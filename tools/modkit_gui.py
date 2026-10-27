#!/usr/bin/env python3
"""Interface do kit de mods (tools/modkit.py): escolher no disco o que extrair, ver e abrir o que foi extraido, empacotar no mod.

  python tools/modkit_gui.py        (ou pythonw, sem console; o launcher tem o botao "Mod kit")

Textos da janela em ingles (pedido do dono). Um mod = um projeto: no topo, "Mod:" escolhe o mod (ou "New mod..." cria um), e ele
governa tudo; cada mod tem a sua pasta de trabalho (modkit.work_dir: <mods>/_work/<mod>). Esquerda: o disco por nome (monstros e
trajes, fases, menu, bancos de som, musicas); duplo clique ou "Extract selected" manda para a pasta de trabalho do mod. Direita: a
pasta de trabalho do mod com miniaturas; "edited" = mudou desde a extracao (vai no proximo pacote). Duplo clique abre a textura no
editor escolhido em "Editors..." (ou no programa padrao do Windows) e toca o som na propria janela; botao direito: Play, Open with...,
Save to folder..., Remove. Skins: "New skin..." (dialogo: monstro, traje base com os retratos, nome, id, CPUs) cria a skin; o grupo
dela na lista ("Skin: ...") mostra as propriedades do skin.json no lugar da previa (Save regrava), e o botao direito duplica ou apaga;
"Turn into skin..." num traje extraido (MON/SHOGUN3) leva os PNGs dele para uma skin nova. Monstros custom: "New monster..."
(dialogo: tipo base, traje, nome, id, camera, narrador, CPUs, texturas proprias) cria o monster.json e extrai o traje para
monsters/<id>/files/; o grupo dele ("Monster: ...") mostra as propriedades (camera, narrador, regras, retrato do HUD, modelo
proprio) e o botao direito duplica, apaga ou cria uma skin dele; "Turn into custom monster..." num traje extraido. Embaixo: "Build mod" (modkit pack so da pasta do mod), "Check mods" (modkit check), abrir a pasta dos mods
e a de trabalho; o log tem divisoria arrastavel. Pastas, ultimo mod, editores e altura do log ficam em ~/.wotm_modkit.json. Precisa
do Pillow (pip install pillow) para as miniaturas.
"""
import io
import json
import os
import queue
import subprocess
import sys
import shutil
import tempfile
import threading
import traceback
import wave
from array import array
from contextlib import redirect_stderr, redirect_stdout

import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modkit  # noqa: E402

try:
    from PIL import Image, ImageTk
except ImportError:   # sem Pillow: sem miniaturas nem previa de imagem
    Image = ImageTk = None

CONF = os.path.join(os.path.expanduser("~"), ".wotm_modkit.json")

# Monstros: prefixo do arquivo (MON/<PREFIXO><traje>) -> nome no jogo, bancos de som (GLOSSARIO.md)
MONSTERS = [
    ("MONKEY", "Congar", ("APE_S", "APE_LS")), ("ROBOT", "Robo-47", ("ROBOT_S", "ROBOT_LS")),
    ("LIZARD", "Togera", ("LIZARD_S", "LIZRD_LS")), ("SHOGUN", "Ultra V", ("SHOGUN_S", "SHOGN_LS")),
    ("MANTIS", "Preytor", ("MANTIS_S", "MANTS_LS")), ("ENERGY", "Kineticlops", ("ENERGY_S", "ENERG_LS")),
    ("LAVA", "Magmo", ("LAVA_S", "LAVA_LS")), ("DRAGON", "Raptros", ("DRAGON_S", "DRAGN_LS")),
    ("ROCK", "Agamo", ("ROKMAN_S", "ROKMN_LS")), ("ALIEN", "Zorgulon", ("ALIEN_S", "ALIEN_LS")),
]
BOSSES = [("ASSBOSS", "Goliath Prime", ("ASSEMB_S",)), ("JELLY", "Jelly (final boss)", ()), ("FINAL", "Final boss", ("FINAL_S",))]
LEVELS = [("CENTRAL", "Midtown Park"), ("VEGAS", "Gamblers Gulch"), ("CANYON2", "Rosdale Canyon"), ("METRO", "Metro City"),
          ("AIRPORT", "Century Airfield"), ("3-MILE", "Atomic Island"), ("SANFRAN", "Baytown"), ("ISLAND", "Club Caldera"),
          ("TOKYO", "Tsunopolis"), ("UFO", "UFO"), ("CAPITOL", "Capitol"), ("TRAINING", "Training"), ("CANYON1", "Lake Powell"),
          ("MINISF", "Mini Baytown"), ("VOLCANO", "Volcano"), ("TOKYODM", "Tokyo Deathmatch"), ("DBALL", "Dodge Ball"),
          ("BIGSHOT", "Big Shot"), ("CRUSH", "Crush")]
SHELL = [("SHELL", "Main menu"), ("SHELL2", "Menu (2)"), ("UI", "Interface"), ("LOAD", "Loading screen"),
         ("PRESHELL", "Before the menu")]

BG, BG2, FG, DIM, ACC, EDIT = "#1e1e1e", "#2b2b2b", "#e6e6e6", "#9a9a9a", "#2f6fd6", "#f0b030"


class QueueWriter(io.TextIOBase):
    def __init__(self, q):
        self.q = q

    def write(self, s):
        if s:
            self.q.put(s)
        return len(s)


class App:
    def __init__(self, root):
        self.root = root
        root.title("WotM - Mod kit")
        root.geometry("1280x800")
        root.minsize(900, 560)
        root.configure(bg=BG)
        self.conf = self.load_conf()
        self.disc = tk.StringVar(value=self.conf.get("disc") or modkit.default_disc())
        self.mods = tk.StringVar(value=self.conf.get("mods") or modkit.default_mods(self.disc.get()))
        self.mod = tk.StringVar(value=self.conf.get("mod") or "")   # mod atual; refresh_mods zera se ele nao existe mais
        self.mod_buttons = []   # botoes que so valem com um mod escolhido (update_mod_state liga/desliga)
        self._shown_work = None   # pasta de trabalho que a lista mostra (trocou: selecao e grupos abertos nao valem)
        self.only_edited = tk.BooleanVar(value=False)
        self.filter = tk.StringVar()
        self.autoplay = tk.BooleanVar(value=self.conf.get("autoplay", True))
        self.editors = dict(self.conf.get("editors") or {})   # ".png" -> executavel; sem entrada: programa padrao do Windows
        self.last_played = None
        self.log_q = queue.Queue()
        self.busy = False
        self.thumbs = {}      # caminho -> (mtime, PhotoImage)
        self.preview_img = None
        self.portraits = {}   # prefixo -> {traje: PhotoImage ou None} (retratos dos trajes, carregados em segundo plano)
        self.portrait_cbs = {}   # prefixo -> [funcoes a chamar quando os retratos chegarem]
        self.portrait_q = queue.Queue()
        self._panel_sid = None   # grupo mostrado no painel de propriedades ("skins/<id>", "monsters/<id>")
        self._customs = {}   # find_customs da ultima refresh_work (rotulos das skins)
        self.style()
        self.build()
        self.fill_disc()
        self.refresh_mods()
        self.refresh_work()
        root.after(100, self.pump_log)
        root.protocol("WM_DELETE_WINDOW", self.close)

    # ------------------------------------------------------------ aparencia
    def style(self):
        s = ttk.Style(self.root)
        s.theme_use("clam")
        s.configure(".", background=BG, foreground=FG, fieldbackground=BG2, bordercolor="#444", lightcolor=BG2, darkcolor=BG2,
                    troughcolor=BG2, arrowcolor=FG, insertcolor=FG)
        s.configure("TButton", background="#3a3a3a", padding=(10, 4))
        s.map("TButton", background=[("active", "#4a4a4a"), ("disabled", "#2a2a2a")], foreground=[("disabled", DIM)])
        s.configure("Accent.TButton", background=ACC)
        s.map("Accent.TButton", background=[("active", "#3f80e8"), ("disabled", "#2a2a2a")])
        s.configure("Treeview", background=BG2, fieldbackground=BG2, foreground=FG, rowheight=22, borderwidth=0)
        s.configure("Work.Treeview", rowheight=44)
        s.map("Treeview", background=[("selected", ACC)], foreground=[("selected", "white")])
        s.configure("Treeview.Heading", background="#333", foreground=FG, relief="flat")
        s.map("Treeview.Heading", background=[("active", "#3d3d3d")])
        s.configure("TEntry", fieldbackground=BG2, foreground=FG)
        s.configure("TCombobox", fieldbackground=BG2, foreground=FG, background="#3a3a3a")
        s.map("TCombobox", fieldbackground=[("readonly", BG2)], foreground=[("readonly", FG)])
        s.configure("TCheckbutton", background=BG, foreground=FG)
        s.map("TCheckbutton", background=[("active", BG)])
        s.configure("TRadiobutton", background=BG, foreground=FG)
        s.map("TRadiobutton", background=[("active", BG)], foreground=[("disabled", DIM)])
        s.configure("Dim.TLabel", foreground=DIM)
        s.configure("Err.TLabel", foreground="#ff6b6b")
        s.configure("Head.TLabel", font=("Segoe UI", 11, "bold"))
        self.root.option_add("*TCombobox*Listbox.background", BG2)
        self.root.option_add("*TCombobox*Listbox.foreground", FG)

    # ------------------------------------------------------------ montagem
    def build(self):
        top = ttk.Frame(self.root, padding=(10, 8, 10, 4))
        top.pack(fill="x")
        for r, (label, var) in enumerate((("Extracted disc", self.disc), ("Mods folder", self.mods))):
            ttk.Label(top, text=label).grid(row=r, column=0, sticky="w", padx=(0, 8), pady=2)
            ttk.Entry(top, textvariable=var).grid(row=r, column=1, sticky="ew", pady=2)
            ttk.Button(top, text="...", width=3, command=lambda v=var: self.pick_dir(v)).grid(row=r, column=2, padx=(4, 0))
        top.columnconfigure(1, weight=1)
        # o mod atual: governa a pasta de trabalho, o Extract e o Build
        ttk.Label(top, text="Mod", style="Head.TLabel").grid(row=2, column=0, sticky="w", padx=(0, 8), pady=(6, 2))
        self.mod_bar = ttk.Frame(top)   # combobox + botoes do mod (a proxima etapa pode acrescentar aqui)
        self.mod_bar.grid(row=2, column=1, columnspan=2, sticky="ew", pady=(6, 2))
        self.mod_box = ttk.Combobox(self.mod_bar, textvariable=self.mod, width=32, state="readonly")
        self.mod_box.pack(side="left")
        self.mod_box.bind("<<ComboboxSelected>>", lambda e: self.on_mod())
        ttk.Button(self.mod_bar, text="New mod...", command=self.new_mod).pack(side="left", padx=(6, 0))
        self.disc.trace_add("write", lambda *_: self.on_paths())
        self.mods.trace_add("write", lambda *_: self.on_paths())

        # divisoria vertical: em cima o trabalho, embaixo o log (arrastar a borda muda a altura do log)
        vsplit = ttk.PanedWindow(self.root, orient="vertical")
        vsplit.pack(fill="both", expand=True)
        upper = ttk.Frame(vsplit)
        vsplit.add(upper, weight=5)
        self.vsplit = vsplit
        panes = ttk.PanedWindow(upper, orient="horizontal")
        panes.pack(fill="both", expand=True, padx=10, pady=4)

        # esquerda: disco
        left = ttk.Frame(panes)
        panes.add(left, weight=3)
        ttk.Label(left, text="1. Pick from the disc", style="Head.TLabel").pack(anchor="w")
        f = ttk.Frame(left)
        f.pack(fill="x", pady=(4, 4))
        ttk.Label(f, text="Search").pack(side="left")
        e = ttk.Entry(f, textvariable=self.filter)
        e.pack(side="left", fill="x", expand=True, padx=(6, 0))
        self.filter.trace_add("write", lambda *_: self.fill_disc())
        ttk.Label(left, text="Ctrl or Shift to pick several; double-click extracts", style="Dim.TLabel", wraplength=230).pack(anchor="w", pady=(0, 2))
        self.disc_tree = ttk.Treeview(left, show="tree", selectmode="extended")
        self.disc_tree.pack(fill="both", expand=True)
        self.disc_tree.bind("<Double-1>", lambda e: self.export_selected())
        b = ttk.Frame(left)
        b.pack(fill="x", pady=(6, 0), side="bottom", before=self.disc_tree)
        self.btn_export = ttk.Button(b, text="Extract selected  >>", style="Accent.TButton", command=self.export_selected)
        self.btn_export.pack(side="right")
        self.mod_buttons.append(self.btn_export)

        # direita: pasta de trabalho + previa
        right = ttk.Frame(panes)
        panes.add(right, weight=5)
        h = ttk.Frame(right)
        h.pack(fill="x")
        self.work_bar = h   # cabecalho da lista de trabalho: titulo e dica a esquerda, botoes a direita
        self.work_head = ttk.Label(h, text="2. Edit the work folder", style="Head.TLabel")
        self.work_head.pack(side="left")
        self.work_hint = ttk.Label(h, text="", style="Dim.TLabel")
        self.work_hint.pack(side="left", padx=(10, 0))
        ttk.Checkbutton(h, text="edited only", variable=self.only_edited, command=self.refresh_work).pack(side="right")
        ttk.Button(h, text="Refresh", command=self.refresh_work).pack(side="right", padx=6)
        self.btn_new_monster = ttk.Button(h, text="New monster...", command=self.new_monster)
        self.btn_new_monster.pack(side="right")
        self.mod_buttons.append(self.btn_new_monster)
        self.btn_new_skin = ttk.Button(h, text="New skin...", command=self.new_skin)
        self.btn_new_skin.pack(side="right", padx=(0, 6))
        self.mod_buttons.append(self.btn_new_skin)
        # so liga com um grupo de traje de monstro jogavel selecionado (MON/SHOGUN3): vira skin com os PNGs dele
        self.btn_turn_skin = ttk.Button(h, text="Turn into skin...", command=self.turn_selected_into_skin, state="disabled")
        self.btn_turn_skin.pack(side="right", padx=6)
        body = ttk.PanedWindow(right, orient="horizontal")
        body.pack(fill="both", expand=True, pady=(4, 0))
        wf = ttk.Frame(body)
        body.add(wf, weight=3)
        self.work_tree = ttk.Treeview(wf, style="Work.Treeview", columns=("info", "state"), selectmode="extended")
        self.work_tree.heading("#0", text="File")
        self.work_tree.heading("info", text="Size")
        self.work_tree.heading("state", text="State")
        self.work_tree.column("#0", width=300)
        self.work_tree.column("info", width=110, anchor="center")
        self.work_tree.column("state", width=80, anchor="center")
        self.work_tree.tag_configure("edited", foreground=EDIT)
        sb = ttk.Scrollbar(wf, orient="vertical", command=self.work_tree.yview)
        self.work_tree.configure(yscrollcommand=sb.set)
        self.work_tree.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")
        self.work_tree.bind("<<TreeviewSelect>>", lambda e: self.on_select())
        self.work_tree.bind("<Double-1>", self.on_double)
        self.work_tree.bind("<Button-3>", self.context_menu)
        self.work_tree.bind("<Delete>", lambda e: self.remove_selected())
        self.menu = tk.Menu(self.root, tearoff=0, bg=BG2, fg=FG, activebackground=ACC, activeforeground="white", bd=0)

        pv = ttk.Frame(body, padding=(8, 0, 0, 0))
        body.add(pv, weight=2)
        self.canvas = tk.Canvas(pv, bg=BG2, highlightthickness=0, height=300)
        self.canvas.bind("<Configure>", lambda e: self.show_preview())
        foot = ttk.Frame(pv)   # legenda e botoes presos embaixo; a previa fica com o que sobra
        foot.pack(side="bottom", fill="x")
        self.pv_label = ttk.Label(foot, text="", style="Dim.TLabel", wraplength=360, justify="left")
        self.pv_label.pack(fill="x", pady=4)
        pb = ttk.Frame(foot)
        pb.pack(fill="x")
        ttk.Button(pb, text="Open in editor", command=self.open_selected).pack(side="left")
        ttk.Button(pb, text="Show in folder", command=self.reveal_selected).pack(side="left", padx=4)
        self.btn_play = ttk.Button(pb, text="Play", command=self.play_selected)
        self.btn_play.pack(side="left")
        ttk.Button(pb, text="Stop", command=self.stop_sound).pack(side="left", padx=4)
        ttk.Button(pb, text="Remove", command=self.remove_selected).pack(side="right")
        pb2 = ttk.Frame(foot)
        pb2.pack(fill="x", pady=(4, 0))
        ttk.Checkbutton(pb2, text="play sounds on click", variable=self.autoplay).pack(side="left")
        ttk.Button(pb2, text="Editors...", command=self.editors_dialog).pack(side="right")
        self.skin_panel = ttk.Frame(pv)   # propriedades da skin: no lugar da previa quando o grupo de uma skin esta escolhido
        # a previa por ultimo: com pouca altura ela encolhe e os botoes ficam
        self.canvas.pack(fill="both", expand=True)

        # embaixo: build + log
        bot = ttk.Frame(upper, padding=(10, 4, 10, 8))
        bot.pack(fill="x", side="bottom", before=panes)   # antes da area elastica: nao some com o log alto
        self.build_bar = bot   # rodape: botoes em fila a esquerda (pack side=left), status a direita
        ttk.Label(bot, text="3. Build", style="Head.TLabel").pack(side="left", padx=(0, 8))
        self.btn_build = ttk.Button(bot, text="Build mod", style="Accent.TButton", command=self.build_mod)
        self.btn_build.pack(side="left")
        self.mod_buttons.append(self.btn_build)
        self.btn_check = ttk.Button(bot, text="Check mods", command=self.check)
        self.btn_check.pack(side="left", padx=6)
        ttk.Button(bot, text="Open mods folder", command=lambda: self.open_path(self.mods.get())).pack(side="left")
        self.btn_open_work = ttk.Button(bot, text="Open work folder", command=self.open_work)
        self.btn_open_work.pack(side="left", padx=6)
        self.mod_buttons.append(self.btn_open_work)
        self.status = ttk.Label(bot, text="", style="Dim.TLabel")
        self.status.pack(side="right")

        lf = ttk.Frame(vsplit, padding=(10, 0, 10, 10))
        vsplit.add(lf, weight=1)
        self.log = tk.Text(lf, height=4, bg="#141414", fg="#cfcfcf", insertbackground=FG, relief="flat", font=("Consolas", 9),
                           wrap="none")
        lsb = ttk.Scrollbar(lf, orient="vertical", command=self.log.yview)
        self.log.configure(yscrollcommand=lsb.set)
        self.log.pack(side="left", fill="both", expand=True)
        lsb.pack(side="right", fill="y")
        vsplit.bind("<ButtonRelease-1>", lambda e: self.clamp_log())
        # altura do log da ultima vez (ou ~9 linhas)
        self.root.after(50, lambda: (self.vsplit.sashpos(0, self.vsplit.winfo_height() - int(self.conf.get("log_height", 170))), self.clamp_log()))
        self.log.tag_configure("warn", foreground=EDIT)
        self.log.tag_configure("err", foreground="#ff6b6b")
        self.say("Mod kit. Pick or create a mod at the top, extract something from the disc into its work folder, edit it and build "
                 "the mod. No game files go into the repo.\n")

    # ------------------------------------------------------------ caminhos e mod atual
    @staticmethod
    def valid_mod_name(n):
        return not modkit.mod_name_error(n)

    def cur_mod(self):
        """O mod escolhido, ou "" sem mod (ou com nome invalido)."""
        n = self.mod.get().strip()
        return n if self.valid_mod_name(n) else ""

    def work(self):
        """Pasta de trabalho do mod atual (modkit.work_dir); None sem mod escolhido."""
        mod = self.cur_mod()
        return modkit.work_dir(self.mods.get(), mod) if mod else None

    def on_mod(self):
        """Trocou o mod: a area de trabalho inteira passa a ser a dele."""
        self.stop_sound()
        self.update_mod_state()
        mod = self.cur_mod()
        if mod:
            # arquivos do mod que a pasta de trabalho nao tem (mod antigo ou feito a mao) entram nela como editados
            try:
                n = modkit.adopt_mod(self.disc.get(), self.mods.get(), mod, self.work())
                if n:
                    self.say("%d file(s) of %s copied into its work folder; they show as edited and go back on Build.\n" % (n, mod))
            except Exception as ex:
                self.say("Could not copy %s's files into the work folder: %s\n" % (mod, ex), "err")
        self.refresh_work()
        self.show_preview()
        self.save_conf()

    def update_mod_state(self):
        mod = self.cur_mod()
        self.work_head.configure(text="2. Edit %s's work folder" % mod if mod else "2. Edit the work folder")
        self.work_hint.configure(text="" if mod else "Pick or create a mod first (top)")
        if not self.busy:
            for b in self.mod_buttons:
                b.state(["!disabled"] if mod else ["disabled"])

    def new_mod(self):
        mods = self.mods.get()
        while True:
            n = simpledialog.askstring("New mod", "Name of the new mod: letters, digits, '_' and '-' only, starting with a letter or digit\n"
                                       "(no spaces; e.g. green_togera or Robo47-skins):", parent=self.root)
            if n is None:
                return
            n = n.strip()
            err = modkit.new_mod_name_error(n)
            if not err:
                break
            messagebox.showwarning("New mod", "\"%s\": %s." % (n, err), parent=self.root)
        try:
            existed = os.path.isdir(os.path.join(mods, n))
            os.makedirs(os.path.join(mods, n), exist_ok=True)
            os.makedirs(modkit.work_dir(mods, n), exist_ok=True)
        except OSError as ex:
            self.say("Could not create mod %s: %s\n" % (n, ex), "err")
            return
        self.say("Mod %s %s (work folder: %s)\n" % (n, "already exists, selected" if existed else "created", modkit.work_dir(mods, n)))
        self.mod.set(n)
        self.refresh_mods()
        self.on_mod()

    def open_work(self):
        w = self.work()
        if w:
            self.open_path(w)

    def pick_dir(self, var):
        d = filedialog.askdirectory(initialdir=var.get() or os.getcwd(), parent=self.root)
        if d:
            var.set(os.path.normpath(d))

    def on_paths(self):
        if getattr(self, "_paths_job", None):
            self.root.after_cancel(self._paths_job)
        self._paths_job = self.root.after(400, lambda: (self.fill_disc(), self.refresh_mods(), self.refresh_work()))

    def load_conf(self):
        try:
            with open(CONF, encoding="utf-8") as f:
                return json.load(f)
        except (OSError, ValueError):
            return {}

    def clamp_log(self):
        """Divisoria do log: no minimo ~3 linhas de log e espaco para as listas e os botoes em cima."""
        try:
            h = self.vsplit.winfo_height()
            self.vsplit.sashpos(0, min(max(self.vsplit.sashpos(0), 360), h - 60))
        except tk.TclError:
            pass

    def save_conf(self):
        c = self.conf
        c.update(disc=self.disc.get(), mods=self.mods.get(), mod=self.mod.get(), autoplay=self.autoplay.get(), editors=self.editors)
        try:
            c["log_height"] = max(60, self.vsplit.winfo_height() - self.vsplit.sashpos(0))
        except tk.TclError:
            pass
        try:
            with open(CONF, "w", encoding="utf-8") as f:
                json.dump(c, f, indent=1)
        except OSError:
            pass

    def close(self):
        self.save_conf()
        self.stop_sound()
        self.root.destroy()

    # ------------------------------------------------------------ disco
    def fill_disc(self):
        t = self.disc_tree
        t.delete(*t.get_children())
        disc = self.disc.get()
        if not os.path.isdir(disc):
            t.insert("", "end", text="(extracted disc not found: pick the folder above)")
            return
        q = self.filter.get().strip().lower()

        def has(rel):
            return modkit.disc_file(disc, rel) is not None

        def add(parent, text, pats, open_=False):
            if q and q not in text.lower() and not any(q in p.lower() for p in pats):
                return None
            return t.insert(parent, "end", text=text, values=(json.dumps(pats),), open=open_)

        def group(text):
            return t.insert("", "end", text=text, open=bool(q))

        g = group("Monsters")
        for prefix, name, banks in MONSTERS:
            items = [("Costume %d  (%s%d)" % (k + 1, prefix, k), ["MON/%s%d" % (prefix, k)]) for k in range(4) if has("MON/%s%d.TEX" % (prefix, k))]
            items += [("Sounds  (%s.BNK)" % b, ["SND/%s.BNK" % b]) for b in banks if has("SND/%s.BNK" % b)]
            self.add_node(t, g, name, items, q, add)
        g = group("Bosses")
        for prefix, name, banks in BOSSES:
            items = [("Form %d  (%s%d)" % (k + 1, prefix, k), ["MON/%s%d" % (prefix, k)]) for k in range(4) if has("MON/%s%d.TEX" % (prefix, k))]
            items += [("Sounds  (%s.BNK)" % b, ["SND/%s.BNK" % b]) for b in banks if has("SND/%s.BNK" % b)]
            self.add_node(t, g, name, items, q, add)
        g = group("Levels")
        for f, name in LEVELS:
            if has("LVL/%s.TEX" % f):
                add(g, "%s  (%s)" % (name, f), ["LVL/%s" % f])
        g = group("Menus and screens")
        for f, name in SHELL:
            if has("SHELL/%s.TEX" % f):
                add(g, "%s  (%s)" % (name, f), ["SHELL/%s" % f])
        used = {b for _, _, bs in MONSTERS + BOSSES for b in bs}
        g = group("Sound banks")
        sd = modkit.disc_file(disc, "SND")
        for n in sorted(os.listdir(sd)) if sd else []:
            if n.upper().endswith(".BNK") and os.path.splitext(n)[0].upper() not in used:
                add(g, n.upper(), ["SND/" + n])
        g = group("Music and voices")
        vd = modkit.disc_file(disc, "VAG")
        for n in sorted(os.listdir(vd)) if vd else []:
            if n.upper().endswith((".VPK", ".VAG")):
                add(g, n.upper(), ["VAG/" + n])
        for g in t.get_children():
            if not t.get_children(g):
                t.delete(g)

    @staticmethod
    def add_node(t, g, name, items, q, add):
        if not items:
            return
        if q and q not in name.lower() and not any(q in txt.lower() or any(q in p.lower() for p in ps) for txt, ps in items):
            return
        node = t.insert(g, "end", text=name, values=(json.dumps([p for _, ps in items for p in ps]),), open=bool(q))
        for txt, ps in items:
            t.insert(node, "end", text=txt, values=(json.dumps(ps),))

    def selected_pats(self):
        pats = []
        for iid in self.disc_tree.selection():
            v = self.disc_tree.item(iid, "values")
            if v:
                pats += json.loads(v[0])
        return list(dict.fromkeys(pats))

    def export_selected(self):
        work = self.work()
        if not work:
            self.say("Pick or create a mod first (\"Mod\" at the top): the files go into that mod's work folder.\n", "warn")
            return
        pats = self.selected_pats()
        if not pats:
            self.say("Pick a monster, costume, level or sound in the list on the left.\n", "warn")
            return
        n = len(pats)
        if n > 12 and not messagebox.askyesno("Extract", "Extract %d files? This can take a few minutes." % n, parent=self.root):
            return
        self.run("Extracting %d file(s) into %s..." % (n, self.cur_mod()), lambda disc=self.disc.get(): modkit.export(disc, work, pats),
                 lambda: self.refresh_work(focus=pats))

    # ------------------------------------------------------------ pasta de trabalho
    # Grupo da pasta de trabalho = arquivo do disco de onde o item saiu ("MON/MONKEY0": .TEX e .RTX juntos; "SND/SHELL_S.BNK")
    @staticmethod
    def group_of(rel):
        parts = rel.split("/")
        if len(parts) >= 3 and parts[0] in ("skins", "monsters"):   # skins/<id>/..., monsters/<id>/...: um grupo por skin ou custom
            return parts[0] + "/" + parts[1]
        d, n = os.path.split(rel)
        pre = d + "/" if d else ""
        m = modkit.PNG_RE.match(n)
        if m:
            return pre + m.group(1).upper()
        mb = modkit.BNK_RE.match(n)
        if mb:
            return pre + mb.group(1).upper() + ".BNK"
        return pre + os.path.splitext(n)[0].upper()

    @staticmethod
    def group_of_pat(pat):
        if pat.replace("\\", "/").startswith(("skins/", "monsters/")):   # refresh_work(focus=["skins/<id>"])
            return "/".join(pat.replace("\\", "/").split("/")[:2])
        stem, ext = os.path.splitext(pat.replace("\\", "/"))
        d, n = os.path.split(stem)
        return (d + "/" if d else "") + n.upper() + (".BNK" if ext.upper() == ".BNK" else "")

    def skin_data(self, sid):
        """skin.json da skin na pasta do mod ({} sem ele)."""
        mod = self.cur_mod()
        return modkit._json_obj(os.path.join(modkit.skin_dir(self.mods.get(), mod, sid), "skin.json")) if mod else {}

    def skin_base_text(self, data):
        b = data.get("base")
        if isinstance(b, int):
            return "base %d" % b
        c = self._customs.get(str(data.get("monster", "")).lower())
        return "base %d from monster.json" % c["costume"] if c else "no base"

    def group_label(self, key):
        if key.startswith("skins/"):
            sid = key[6:]
            data = self.skin_data(sid)
            if not data:
                return "Skin: %s  (no skin.json in the mod)" % sid
            return "Skin: %s  (%s/%s, %s)" % (data.get("name") or sid, data.get("monster", "?"), data.get("id", sid), self.skin_base_text(data))
        if key.startswith("monsters/"):
            cid = key[9:]
            data = self.monster_data(cid)
            if not data:
                return "Monster: %s  (no monster.json in the mod)" % cid
            return "Monster: %s  (custom %s, base %s costume %s%s)" % (data.get("name") or cid, data.get("id", cid), data.get("base", "?"),
                                                                       data.get("costume", 0), ", own look" if data.get("files") else "")
        n = key.split("/")[-1]
        for prefix, name, banks in MONSTERS + BOSSES:
            if n[:-1] == prefix and n[-1:].isdigit():
                return "%s  (%s, %s %d)" % (key, name, "costume" if (prefix, name, banks) in MONSTERS else "form", int(n[-1]) + 1)
            if n.endswith(".BNK") and n[:-4] in banks:
                return "%s  (%s sounds)" % (key, name)
        for f, name in LEVELS + SHELL:
            if n == f:
                return "%s  (%s)" % (key, name)
        return key

    def refresh_work(self, focus=None):
        """focus: padroes do export que acabou de rodar; os grupos deles abrem (os outros fecham) e a lista rola ate o primeiro."""
        t = self.work_tree
        work = self.work()
        if work != self._shown_work:   # outro mod: a selecao e os grupos abertos eram da pasta anterior
            t.selection_set(())
            t.delete(*t.get_children())
            self._shown_work = work
        sel = set(t.selection())
        was_open = {i for i in t.get_children() if t.item(i, "open")}
        t.delete(*t.get_children())
        self.update_mod_state()
        if not work:
            self.status.configure(text="no mod picked")
            return
        if not os.path.isdir(work):
            self.status.configure(text="work folder is empty")
            return
        state = modkit.load_state(work)["files"]
        self._customs = modkit.find_customs(self.mods.get())
        groups = {"skins/" + sid: [] for sid in modkit.list_skins(self.mods.get(), self.cur_mod())}   # skin sem PNG tambem aparece
        groups.update({"monsters/" + cid: [] for cid in modkit.list_mod_monsters(self.mods.get(), self.cur_mod())})
        # chave -> [(rel, p, edited)]
        edited_total = total = 0
        for root, ds, files in os.walk(work):
            ds[:] = sorted(d for d in ds if not d.startswith("."))
            for n in sorted(files):
                if n.endswith(".json") or n.startswith("."):
                    continue
                p = os.path.join(root, n)
                rel = os.path.relpath(p, work).replace("\\", "/")
                edited = state.get(rel, {}).get("sha1") != modkit.sha1(p)
                total += 1
                edited_total += edited
                if self.only_edited.get() and not edited:
                    continue
                groups.setdefault(self.group_of(rel), []).append((rel, p, edited))
        want = {self.group_of_pat(p) for p in focus} if focus else None
        first_new = None
        for key in sorted(groups, key=lambda k: (not k.startswith("monsters/"), not k.startswith("skins/"), k)):   # customs e skins primeiro
            items = groups[key]
            gid = "grp:" + key
            if want is not None:
                is_open = key in want
            elif was_open or sel:
                is_open = gid in was_open
            else:
                is_open = len(groups) == 1
            ne = sum(1 for _, _, e in items if e)
            t.insert("", "end", iid=gid, text="%s   [%d file(s)%s]" % (self.group_label(key), len(items), ", %d edited" % ne if ne else ""),
                     open=is_open, tags=("edited",) if ne else ())
            for rel, p, edited in items:
                img = self.thumb(p)
                kw = {"image": img} if img else {}
                t.insert(gid, "end", iid=rel, text=" " + os.path.basename(rel), values=(self.info(p), "edited" if edited else "original"),
                         tags=("edited",) if edited else (), **kw)
            if want is not None and key in want and first_new is None:
                first_new = gid
        if first_new and first_new.startswith(("grp:skins/", "grp:monsters/")):   # o grupo, para o painel de propriedades aparecer
            t.selection_set([first_new])
            t.see(first_new)
        elif first_new:   # seleciona a primeira textura, nao o grupo (um Remove logo depois nao pega o grupo inteiro)
            kids = t.get_children(first_new)
            t.selection_set([kids[0]] if kids else [first_new])
            if kids:
                t.see(kids[min(len(kids) - 1, 3)])
            t.see(first_new)
        else:
            keep = [i for i in sel if t.exists(i)]
            if keep:
                t.selection_set(keep)
                t.see(keep[0])
        self.status.configure(text="%d file(s), %d edited" % (total, edited_total))

    def thumb(self, p):
        if Image is None or not p.lower().endswith(".png"):
            return None
        mt = os.path.getmtime(p)
        c = self.thumbs.get(p)
        if c and c[0] == mt:
            return c[1]
        try:
            im = Image.open(p).convert("RGBA")
            im.thumbnail((40, 40))
            bg = Image.new("RGBA", (40, 40), BG2)
            bg.alpha_composite(im, ((40 - im.width) // 2, (40 - im.height) // 2))
            ph = ImageTk.PhotoImage(bg)
        except Exception:
            return None
        self.thumbs[p] = (mt, ph)
        return ph

    @staticmethod
    def info(p):
        n = p.lower()
        try:
            if n.endswith(".png") and Image is not None:
                with Image.open(p) as im:
                    return "%dx%d" % im.size
            if n.endswith(".wav"):
                with wave.open(p) as w:
                    return "%.1f s" % (w.getnframes() / float(w.getframerate()))
        except Exception:
            pass
        return "%d KB" % max(1, os.path.getsize(p) // 1024)

    def selected_files(self, groups=False):
        """Arquivos escolhidos na lista. groups=True: um grupo selecionado vale por todos os arquivos dele (Save to folder,
        Remove); senao so as linhas de arquivo (abrir, tocar, previa: clicar numa secao nao abre tudo dela)."""
        t, out, work = self.work_tree, [], self.work()
        if not work:
            return out
        for i in t.selection():
            if i.startswith("grp:") and not groups:
                continue
            for r in (t.get_children(i) if i.startswith("grp:") else (i,)):
                p = os.path.join(work, *r.split("/"))
                if p not in out:
                    out.append(p)
        return out

    def show_preview(self):
        c = self.canvas
        c.delete("all")
        sid, cid = self.selected_skin(), self.selected_monster()
        if sid or cid:   # grupo de uma skin ou de um custom: o painel de propriedades no lugar da imagem
            if c.winfo_manager():
                c.pack_forget()
            if not self.skin_panel.winfo_manager():
                self.skin_panel.pack(fill="both", expand=True)
            if sid:
                if "skins/" + sid != self._panel_sid:
                    self.fill_skin_panel(sid)
                self.pv_label.configure(text="Skin %s: its PNGs are in the work folder (skins/%s); open the section to edit them. "
                                             "\"Build mod\" sends the edited ones to the mod's skins/%s." % (sid, sid, sid))
            else:
                if "monsters/" + cid != self._panel_sid:
                    self.fill_monster_panel(cid)
                self.pv_label.configure(text="Custom monster %s: its PNGs are in the work folder (monsters/%s/files); open the section "
                                             "to edit them. \"Build mod\" sends the edited ones to the mod, where they are its own look "
                                             "(only for it)." % (cid, cid))
            return
        if self.skin_panel.winfo_manager():
            self.skin_panel.pack_forget()
            self._panel_sid = None
        if not c.winfo_manager():
            c.pack(fill="both", expand=True)
        files = self.selected_files()
        allf = self.selected_files(groups=True)
        if not files and allf:
            self.pv_label.configure(text="%d file(s) in the selected section. Open it (arrow or double-click) and pick a file; "
                                         "right-click saves or removes the whole section." % len(allf))
            return
        if not files:
            self.pv_label.configure(text="Select a file in the work folder. Double-click opens a texture in its editor (\"Editors...\") "
                                         "and plays a sound; right-click for more (Save to folder...).")
            return
        p = files[0]
        w, h = max(c.winfo_width(), 50), max(c.winfo_height(), 50)
        text = os.path.basename(p)
        if p.lower().endswith(".png") and Image is not None:
            try:
                im = Image.open(p).convert("RGBA")
                text += "  %dx%d" % im.size
                s = min((w - 16) / im.width, (h - 16) / im.height)
                s = s if s < 1 else max(1, int(s))   # amplia em inteiros (pixels nitidos)
                im2 = im.resize((max(1, int(im.width * s)), max(1, int(im.height * s))), Image.NEAREST if s >= 1 else Image.LANCZOS)
                chk = Image.new("RGBA", im2.size, "#3a3a3a")   # xadrez atras do alfa
                light = Image.new("RGBA", (8, 8), "#4a4a4a")
                for y in range(0, im2.height, 16):
                    for x in range(0, im2.width, 16):
                        chk.paste(light, (x, y))
                        chk.paste(light, (x + 8, y + 8))
                chk.alpha_composite(im2)
                self.preview_img = ImageTk.PhotoImage(chk)
                c.create_image(w // 2, h // 2, image=self.preview_img)
            except Exception as ex:
                text += "  (could not open: %s)" % ex
            m = modkit.PNG_RE.match(os.path.basename(p))
            if m:
                text += "\nTexture %d of %s%s. Save it at any resolution, keeping the aspect ratio." % (
                    int(m.group(3)), m.group(1).upper(), ".RTX" if m.group(2) else ".TEX")
        elif p.lower().endswith(".wav"):
            text += "  " + self.info(p)
            if not self.waveform(p, w, h):
                c.create_text(w // 2, h // 2, text="sound", fill=DIM, font=("Segoe UI", 20))
            mb = modkit.BNK_RE.match(os.path.basename(p))
            text += "\n%s" % ("Sound %d of bank %s.BNK" % (int(mb.group(2)), mb.group(1).upper()) if mb else "Music or voice (.VPK/.VAG)")
        if len(files) > 1:
            text += "\n(%d selected)" % len(files)
        self.pv_label.configure(text=text)

    def on_select(self):
        self.show_preview()
        key = self.selected_group()
        self.btn_turn_skin.state(["!disabled"] if (key and self.costume_of_group(key) and not self.busy) else ["disabled"])
        files = self.selected_files()
        if self.autoplay.get() and len(files) == 1 and files[0].lower().endswith(".wav") and files[0] != self.last_played:
            self.play_selected()

    def on_double(self, e):
        row = self.work_tree.identify_row(e.y)
        if not row or row.startswith("grp:"):
            return   # duplo clique no grupo so abre/fecha
        if row.lower().endswith(".wav") and not self.editors.get(".wav"):
            self.play_selected()   # som toca aqui; com editor de .wav definido, abre nele
        else:
            self.open_selected()

    @staticmethod
    def ext_of(p):
        return os.path.splitext(p)[1].lower()

    def waveform(self, p, w, h):
        """Desenha a forma de onda do WAV (PCM 8/16 bits) na previa. False se nao deu."""
        try:
            with wave.open(p) as wf:
                sw, ch, n = wf.getsampwidth(), wf.getnchannels(), wf.getnframes()
                raw = wf.readframes(min(n, 4_000_000))
        except Exception:
            return False
        if sw == 2:
            a = array("h")
            a.frombytes(raw[:len(raw) // 2 * 2])
            if sys.byteorder == "big":
                a.byteswap()
            full = 32768.0
        elif sw == 1:
            a = [b - 128 for b in raw]
            full = 128.0
        else:
            return False
        a = a[::ch]   # so o primeiro canal
        if not len(a):
            return False
        cols = max(1, w - 16)
        step = max(1, len(a) // cols)
        mid, amp = h / 2, (h - 24) / 2
        c = self.canvas
        c.create_line(8, mid, w - 8, mid, fill="#444")
        for x in range(cols):
            seg = a[x * step:(x + 1) * step]
            if not len(seg):
                break
            lo, hi = min(seg), max(seg)
            c.create_line(8 + x, mid - hi / full * amp, 8 + x, mid - lo / full * amp + 1, fill=ACC)
        return True

    def open_selected(self):
        for p in self.selected_files()[:8]:
            exe = self.editors.get(self.ext_of(p))
            if exe:
                self.open_with(p, exe)
            else:
                self.open_path(p)

    def open_with(self, p, exe):
        try:
            subprocess.Popen([exe, os.path.normpath(p)])
        except OSError as ex:
            self.say("Could not start %s: %s\n" % (exe, ex), "err")

    def pick_exe(self, ext):
        exe = filedialog.askopenfilename(parent=self.root, title="Program to open %s files" % ext,
                                         filetypes=[("Programs", "*.exe"), ("All files", "*.*")],
                                         initialdir=os.path.dirname(self.editors.get(ext, "")) or os.environ.get("ProgramFiles", "C:\\"))
        return os.path.normpath(exe) if exe else None

    def open_with_dialog(self):
        files = self.selected_files()
        if not files:
            return
        ext = self.ext_of(files[0])
        exe = self.pick_exe(ext)
        if not exe:
            return
        if messagebox.askyesno("Open with", "Always open %s files with %s?" % (ext, os.path.basename(exe)), parent=self.root):
            self.editors[ext] = exe
            self.save_conf()
        for p in files[:8]:
            self.open_with(p, exe)

    def editors_dialog(self):
        d = tk.Toplevel(self.root, bg=BG)
        d.title("Editors")
        d.transient(self.root)
        d.resizable(True, False)
        fr = ttk.Frame(d, padding=12)
        fr.pack(fill="both", expand=True)
        ttk.Label(fr, text="Program used by \"Open in editor\" and double-click, per file type.\nEmpty = the Windows default "
                           "(for sounds: play here in the kit).", style="Dim.TLabel", justify="left").grid(row=0, column=0, columnspan=4,
                                                                                                         sticky="w", pady=(0, 8))
        rows = {}
        exts = [".png", ".wav"] + sorted(e for e in self.editors if e not in (".png", ".wav"))
        for r, ext in enumerate(exts, 1):
            v = tk.StringVar(value=self.editors.get(ext, ""))
            rows[ext] = v
            ttk.Label(fr, text={".png": "Textures (.png)", ".wav": "Sounds (.wav)"}.get(ext, ext)).grid(row=r, column=0, sticky="w", padx=(0, 8))
            ttk.Entry(fr, textvariable=v, width=60).grid(row=r, column=1, sticky="ew", pady=2)
            ttk.Button(fr, text="...", width=3, command=lambda e=ext, v=v: v.set(self.pick_exe(e) or v.get())).grid(row=r, column=2, padx=4)
            ttk.Button(fr, text="Clear", command=lambda v=v: v.set("")).grid(row=r, column=3)
        fr.columnconfigure(1, weight=1)

        def ok():
            for ext, v in rows.items():
                exe = v.get().strip().strip('"')
                if exe:
                    self.editors[ext] = exe
                else:
                    self.editors.pop(ext, None)
            self.save_conf()
            d.destroy()

        b = ttk.Frame(fr)
        b.grid(row=len(exts) + 1, column=0, columnspan=4, sticky="e", pady=(10, 0))
        ttk.Button(b, text="OK", style="Accent.TButton", command=ok).pack(side="left")
        ttk.Button(b, text="Cancel", command=d.destroy).pack(side="left", padx=(6, 0))
        d.grab_set()

    def context_menu(self, e):
        t = self.work_tree
        row = t.identify_row(e.y)
        if row and row not in t.selection():
            t.selection_set(row)
            t.focus(row)
        files = self.selected_files()
        allf = self.selected_files(groups=True)
        m = self.menu
        m.delete(0, "end")
        sid, cid, key = self.selected_skin(), self.selected_monster(), self.selected_group()
        if sid:
            m.add_command(label="Duplicate skin...", command=lambda: self.duplicate_skin(sid))
            m.add_command(label="Delete skin", command=lambda: self.delete_skin(sid))
            if allf:
                m.add_separator()
        elif cid:
            m.add_command(label="New skin for this monster...", command=lambda: self.new_skin(preset=cid))
            m.add_command(label="Duplicate monster...", command=lambda: self.duplicate_monster(cid))
            m.add_command(label="Delete monster", command=lambda: self.delete_monster(cid))
            if allf:
                m.add_separator()
        elif key and self.costume_of_group(key, bosses=True):
            if self.costume_of_group(key):
                m.add_command(label="Turn into skin...", command=lambda: self.turn_into_skin(key))
            m.add_command(label="Turn into custom monster...", command=lambda: self.turn_into_monster(key))
            m.add_separator()
        if (sid or cid) and not allf:
            pass
        elif not allf:
            if not self.work():
                return
            m.add_command(label="Open work folder", command=self.open_work)
        elif not files:   # so secoes: nada de abrir/tocar tudo de uma vez
            n = len(allf)
            m.add_command(label="Show in folder", command=self.reveal_selected)
            m.add_separator()
            m.add_command(label="Save to folder... (%d file%s)" % (n, "" if n == 1 else "s"), command=self.save_to_folder)
            self.add_revert(m)
            m.add_separator()
            m.add_command(label="Remove from work folder (%d file%s)" % (n, "" if n == 1 else "s"), command=self.remove_selected)
        else:
            n = len(allf)
            wav = any(p.lower().endswith(".wav") for p in files)
            if wav:
                m.add_command(label="Play", command=self.play_selected)
            exe = self.editors.get(self.ext_of(files[0]))
            m.add_command(label="Open in %s" % (os.path.splitext(os.path.basename(exe))[0] if exe else "default program"),
                          command=self.open_selected)
            m.add_command(label="Open with...", command=self.open_with_dialog)
            if len(files) == 1:
                m.add_command(label="Replace with...", command=self.replace_selected)
            m.add_command(label="Show in folder", command=self.reveal_selected)
            m.add_separator()
            m.add_command(label="Save to folder... (%d file%s)" % (n, "" if n == 1 else "s"), command=self.save_to_folder)
            self.add_revert(m)
            m.add_separator()
            m.add_command(label="Remove from work folder", command=self.remove_selected)
        m.tk_popup(e.x_root, e.y_root)
        m.grab_release()

    IMG_EXT = (".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".tif", ".tiff", ".webp", ".dds")

    def replace_selected(self):
        """Troca um asset da pasta de trabalho por um arquivo escolhido, gravado com o nome certo. Imagem de outro formato vira PNG."""
        files = self.selected_files()
        if len(files) != 1:
            return
        dst = files[0]
        ext = self.ext_of(dst)
        if ext == ".png":
            types = [("Images", " ".join("*" + e for e in self.IMG_EXT)), ("All files", "*.*")]
        elif ext == ".wav":
            types = [("WAV sound", "*.wav"), ("All files", "*.*")]
        else:
            types = [("All files", "*.*")]
        src = filedialog.askopenfilename(parent=self.root, title="Replace %s with" % os.path.basename(dst), filetypes=types,
                                         initialdir=self.conf.get("replace_dir") or self.conf.get("save_dir") or os.path.expanduser("~"))
        if not src:
            return
        src = os.path.normpath(src)
        self.conf["replace_dir"] = os.path.dirname(src)
        sext = self.ext_of(src)
        if os.path.normcase(src) == os.path.normcase(os.path.normpath(dst)):
            return
        orig = self.orig_size(dst)
        try:
            if ext == ".png" and sext != ".png":
                if Image is None:
                    raise OSError("converting %s to PNG needs Pillow (pip install pillow)" % sext)
                with Image.open(src) as im:
                    im.convert("RGBA").save(dst, "PNG")
            elif ext == ".wav" and sext != ".wav":
                raise OSError("pick a .wav file (the kit converts WAV to the game's format when packing)")
            elif ext not in (".png", ".wav") and sext != ext:
                raise OSError("pick a %s file" % ext)
            else:
                if ext == ".wav":
                    with wave.open(src):   # confere que e um WAV que o pack consegue ler
                        pass
                shutil.copyfile(src, dst)   # copyfile: mtime novo, a miniatura atualiza
        except Exception as ex:
            self.say("Could not replace %s: %s\n" % (os.path.basename(dst), ex), "err")
            return
        self.stop_sound()
        msg = "Replaced %s with %s" % (os.path.basename(dst), os.path.basename(src))
        new = modkit.png_size(dst) if ext == ".png" else None
        if new:
            msg += " (%dx%d)" % new
        self.say(msg + "\n")
        if new and orig and new[0] * orig[1] != new[1] * orig[0]:
            self.say("WARNING: the game texture is %dx%d; a different aspect ratio looks stretched in the game\n" % orig, "warn")
        self.save_conf()
        self.refresh_work()
        rel = os.path.relpath(dst, self.work()).replace("\\", "/")
        if self.work_tree.exists(rel):
            self.work_tree.selection_set([rel])
            self.work_tree.see(rel)
        self.show_preview()

    def orig_size(self, p):
        """Tamanho da textura no jogo (largura, altura): o que o export guardou ou, em export antigo, o do proprio arquivo enquanto
        ele nao foi editado. None se nao der para saber."""
        st = modkit.load_state(self.work())["files"].get(os.path.relpath(p, self.work()).replace("\\", "/")) or {}
        if st.get("size"):
            return tuple(st["size"])
        if st.get("sha1") and os.path.exists(p) and modkit.sha1(p) == st["sha1"]:
            return modkit.png_size(p)
        return None

    def save_to_folder(self):
        files = self.selected_files(groups=True)
        if not files:
            return
        d = filedialog.askdirectory(parent=self.root, title="Save %d file(s) to" % len(files),
                                    initialdir=self.conf.get("save_dir") or os.path.expanduser("~"))
        if not d:
            return
        d = os.path.normpath(d)
        self.conf["save_dir"] = d
        exist = [p for p in files if os.path.exists(os.path.join(d, os.path.basename(p)))]
        if exist and not messagebox.askyesno("Save to folder", "%d file(s) already exist in %s. Overwrite?" % (len(exist), d),
                                             parent=self.root):
            files = [p for p in files if p not in exist]
        n = 0
        for p in files:
            try:
                shutil.copy2(p, os.path.join(d, os.path.basename(p)))
                n += 1
            except OSError as ex:
                self.say("%s\n" % ex, "err")
        self.say("Saved %d file(s) to %s\n" % (n, d))
        self.save_conf()

    def reveal_selected(self):
        files = self.selected_files(groups=True)
        if files:
            subprocess.Popen(["explorer", "/select,", os.path.normpath(files[0])])
        else:
            self.open_work()

    @staticmethod
    def open_path(p):
        if not os.path.exists(p) and not os.path.splitext(p)[1]:
            os.makedirs(p, exist_ok=True)   # pasta de mods ou de trabalho ainda nao criada
        try:
            os.startfile(os.path.normpath(p))
        except (OSError, AttributeError) as ex:
            messagebox.showerror("Open", str(ex))

    def play_selected(self):
        wavs = [p for p in self.selected_files() if p.lower().endswith(".wav")]
        if not wavs:
            return
        self.last_played = wavs[0]
        try:
            import winsound
            winsound.PlaySound(wavs[0], winsound.SND_FILENAME | winsound.SND_ASYNC | winsound.SND_NODEFAULT)
        except Exception as ex:
            self.say("Could not play: %s\n" % ex, "err")

    def stop_sound(self):
        self.last_played = None
        try:
            import winsound
            winsound.PlaySound(None, 0)
        except Exception:
            pass

    def edited_selected(self):
        """Arquivos editados na selecao (um grupo vale pelos editados dele)."""
        work = self.work()
        st = modkit.load_state(work)["files"]
        out = []
        for p in self.selected_files(groups=True):
            old = st.get(os.path.relpath(p, work).replace("\\", "/"), {}).get("sha1")
            if not old or not os.path.exists(p) or modkit.sha1(p) != old:
                out.append(p)
        return out

    def add_revert(self, m):
        n = len(self.edited_selected())
        if n:
            m.add_command(label="Revert to original" + (" (%d edited)" % n if n > 1 else ""), command=self.revert_selected)

    def revert_selected(self):
        files = self.edited_selected()
        if not files:
            self.say("Nothing to revert: the selected files are as extracted.\n")
            return
        names = "\n".join(os.path.basename(p) for p in files[:10]) + ("\n..." if len(files) > 10 else "")
        if not messagebox.askyesno("Revert to original", "Put %d file(s) back as they are on the disc?\nYour changes to them are lost.\n\n%s"
                                   % (len(files), names), parent=self.root):
            return
        self.stop_sound()
        work = self.work()
        rels = [os.path.relpath(p, work).replace("\\", "/") for p in files]
        sel = self.work_tree.selection()

        def done():
            self.refresh_work()
            keep = [i for i in sel if self.work_tree.exists(i)]
            if keep:
                self.work_tree.selection_set(keep)
            self.show_preview()

        self.run("Reverting %d file(s)..." % len(rels), lambda disc=self.disc.get(): modkit.revert(disc, work, rels), done)

    def remove_selected(self):
        files = self.selected_files(groups=True)
        if not files or not messagebox.askyesno("Remove", "Delete %d file(s) from the work folder?\n(the disc and the mods do not change)"
                                                % len(files), parent=self.root):
            return
        self.stop_sound()
        work = self.work()
        st = modkit.load_state(work)
        for p in files:
            try:
                os.remove(p)
            except OSError as ex:
                self.say("%s\n" % ex, "err")
            st["files"].pop(os.path.relpath(p, work).replace("\\", "/"), None)
        modkit.save_state(work, st)
        self.refresh_work()

    # ------------------------------------------------------------ skins (MODS.md, "Skins")
    def selected_group(self):
        """Chave do grupo, se a selecao e um grupo so ("MON/SHOGUN3", "skins/verde"); None senao."""
        sel = self.work_tree.selection()
        return sel[0][4:] if len(sel) == 1 and sel[0].startswith("grp:") else None

    def selected_skin(self):
        """Id da skin cujo grupo esta escolhido (sozinho); None senao."""
        key = self.selected_group()
        return key[6:] if key and key.startswith("skins/") else None

    @staticmethod
    def costume_of_group(key, bosses=False):
        """(nome do jogo, prefixo, traje) de um grupo de traje de monstro jogavel ("MON/SHOGUN3" -> ("ultra-v", "SHOGUN", 3)); None senao.
        bosses: tambem os chefes que um custom aceita de base ("MON/ASSBOSS1" -> ("assboss", "ASSBOSS", 1))."""
        d, _, n = key.rpartition("/")
        if d.upper() != "MON" or not n[-1:].isdigit() or int(n[-1]) > 3:
            return None
        names = dict(modkit.MONSTERS, **(modkit.CUSTOM_BASES if bosses else {}))
        game = next((g for g, p in names.items() if p == n[:-1].upper()), None)
        return (game, n[:-1].upper(), int(n[-1])) if game else None

    # retratos dos trajes (miniaturas dos botoes de base): o .RTX vira PNG numa thread; a PhotoImage nasce na thread da janela
    def portraits_for(self, prefix, cb=None):
        """{traje: PhotoImage ou None} do prefixo, ou None enquanto carrega (cb() e chamada quando chegar)."""
        if prefix in self.portraits:
            return self.portraits[prefix]
        if Image is None:
            return {}
        first = prefix not in self.portrait_cbs
        cbs = self.portrait_cbs.setdefault(prefix, [])
        if cb:
            cbs.append(cb)
        if first:
            threading.Thread(target=self._load_portraits, args=(self.disc.get(), prefix), daemon=True).start()
        return None

    def _load_portraits(self, disc, prefix):
        out = {}
        tmp = tempfile.mkdtemp(prefix="wotm_portraits_")
        try:
            for k in range(4):
                p = os.path.join(tmp, "%d.png" % k)
                out[k] = None
                try:
                    if modkit.portrait_png(disc, prefix, k, p):
                        with Image.open(p) as im:
                            out[k] = im.convert("RGBA")
                except Exception:
                    pass
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
        self.portrait_q.put((prefix, out))

    def pump_portraits(self):
        try:
            while True:
                prefix, ims = self.portrait_q.get_nowait()
                pics = {}
                for k, im in ims.items():
                    if im is None:
                        pics[k] = None
                        continue
                    im = im.copy()
                    im.thumbnail((96, 48))
                    pics[k] = ImageTk.PhotoImage(im)
                self.portraits[prefix] = pics
                for cb in self.portrait_cbs.pop(prefix, []):
                    try:
                        cb()
                    except tk.TclError:
                        pass   # o dialogo fechou antes
        except queue.Empty:
            pass

    def costume_radios(self, parent, var, prefix, enabled=None, extra=None):
        """Botoes de radio 0 a 3 (com o retrato de cada traje do prefixo, quando ha) numa linha; enabled: trajes que podem ser
        escolhidos (padrao: os que o disco tem). extra: (texto, valor) de mais um botao no fim. Devolve a lista dos botoes."""
        disc = self.disc.get()
        radios = []
        for k in range(4):
            rb = ttk.Radiobutton(parent, text=str(k), variable=var, value=k, compound="top")
            rb.grid(row=0, column=k, padx=(0, 6), sticky="n")
            radios.append(rb)
        if extra:
            rb = ttk.Radiobutton(parent, text=extra[0], variable=var, value=extra[1])
            rb.grid(row=0, column=4, padx=(0, 6), sticky="s")
            radios.append(rb)

        def paint():
            pics = self.portraits.get(prefix, {}) if prefix else {}
            for k, rb in enumerate(radios[:4]):
                if not rb.winfo_exists():
                    return
                ok = bool(prefix) and modkit.disc_file(disc, "MON/%s%d.TEX" % (prefix, k)) is not None
                if enabled is not None:
                    ok = ok and k in enabled
                loading = bool(prefix) and Image is not None and prefix not in self.portraits
                rb.configure(image=pics.get(k) or "", text=str(k) + ("\n(loading)" if loading and ok else ""))
                rb.state(["!disabled"] if ok else ["disabled"])

        if prefix:
            self.portraits_for(prefix, paint)
        paint()
        return radios

    def monster_choices(self):
        """[(texto da lista, chave do skin.json, prefixo, traje padrao, jogavel)]: os dez jogaveis e os customs conhecidos."""
        names = {p: n for p, n, _ in MONSTERS}
        mods = self.mods.get()
        customs = modkit.find_customs(mods)
        out = []
        for key, prefix, co in modkit.list_monsters(mods):
            if co is None:
                out.append((names.get(prefix, key.capitalize()), key, prefix, 0, True))
            else:
                out.append(("%s  (custom, %s)" % (key, customs[key]["mod"]), key, prefix, co, False))
        return out

    @staticmethod
    def resolve_monster(text, choices):
        """(chave, prefixo, traje padrao, tipo) do texto do campo Monster; tipo "game", "custom" ou "unknown" (id de custom que o
        kit nao conhece: sem prefixo nem traje). None se nao serve nem como id."""
        t = text.strip()
        for label, key, prefix, co, game in choices:
            if t == label:
                return key, prefix, co, "game" if game else "custom"
        g = modkit.game_monster(t) if t else None
        if g:
            return g, modkit.MONSTERS[g], 0, "game"
        k = t.lower()
        for label, key, prefix, co, game in choices:
            if not game and key == k:
                return key, prefix, co, "custom"
        if modkit.CUSTOM_ID_RE.fullmatch(k):
            return k, None, None, "unknown"
        return None

    def skin_exists(self, sid):
        mod = self.cur_mod()
        return bool(mod) and (os.path.exists(modkit.skin_dir(self.mods.get(), mod, sid)) or
                              os.path.exists(os.path.join(self.work(), "skins", sid)))

    def free_skin_id(self, stem="skin"):
        for i in range(1, 1000):
            sid = "%s%d" % (stem, i)
            if not self.skin_exists(sid):
                return sid
        return stem

    def skin_dialog(self, title, on_ok, lock=None, preset=None):
        """Dialogo de skin nova: Monster, Base costume, Name, Id, CPUs. lock = (nome do jogo, prefixo, traje): monstro e base
        travados (Turn into skin). preset: id de um custom que ja vem escolhido. on_ok(chave, id, base ou None, nome, ai)."""
        d = tk.Toplevel(self.root, bg=BG)
        d.title(title)
        d.transient(self.root)
        d.resizable(False, False)
        fr = ttk.Frame(d, padding=12)
        fr.pack(fill="both", expand=True)
        choices = self.monster_choices()
        mon, base, name, sid, ai = tk.StringVar(), tk.IntVar(value=0), tk.StringVar(), tk.StringVar(), tk.BooleanVar(value=False)
        st = {"res": None, "prefix": "?", "auto_name": True, "auto_id": True, "guard": False, "radios": []}

        ttk.Label(fr, text="Monster").grid(row=0, column=0, sticky="w", padx=(0, 8), pady=2)
        box = ttk.Combobox(fr, textvariable=mon, values=[c[0] for c in choices], width=40, state="disabled" if lock else "normal")
        box.grid(row=0, column=1, sticky="ew", pady=2)
        hint = ttk.Label(fr, text="", style="Dim.TLabel", wraplength=440, justify="left")
        hint.grid(row=1, column=1, sticky="w")
        ttk.Label(fr, text="Base costume").grid(row=2, column=0, sticky="nw", padx=(0, 8), pady=(8, 2))
        rb_fr = ttk.Frame(fr)
        rb_fr.grid(row=2, column=1, sticky="w", pady=(8, 2))
        ttk.Label(fr, text="Name").grid(row=3, column=0, sticky="w", padx=(0, 8), pady=2)
        ttk.Entry(fr, textvariable=name, width=40).grid(row=3, column=1, sticky="ew", pady=2)
        ttk.Label(fr, text="Id").grid(row=4, column=0, sticky="w", padx=(0, 8), pady=2)
        ttk.Entry(fr, textvariable=sid, width=40).grid(row=4, column=1, sticky="ew", pady=2)
        ttk.Label(fr, text="Folder name and what the save keeps: lowercase letters, digits and _", style="Dim.TLabel").grid(
            row=5, column=1, sticky="w")
        ttk.Checkbutton(fr, text="CPUs may pick this skin", variable=ai).grid(row=6, column=1, sticky="w", pady=(6, 0))
        err = ttk.Label(fr, text="", style="Err.TLabel", wraplength=440, justify="left")
        err.grid(row=7, column=0, columnspan=2, sticky="w", pady=(6, 0))
        bf = ttk.Frame(fr)
        bf.grid(row=8, column=0, columnspan=2, sticky="e", pady=(10, 0))
        btn_ok = ttk.Button(bf, text="OK", style="Accent.TButton")
        btn_ok.pack(side="left")
        ttk.Button(bf, text="Cancel", command=d.destroy).pack(side="left", padx=(6, 0))
        fr.columnconfigure(1, weight=1)

        def set_quiet(var, v):
            st["guard"] = True
            var.set(v)
            st["guard"] = False

        def default_name():
            res = st["res"]
            return ("%s %s" % (sid.get(), res[0] if res else "")).strip().upper()

        def validate():
            res, msg = st["res"], ""
            e = modkit.skin_id_error(sid.get())
            if res is None:
                msg = "Monster: a playable monster or the id of a custom monster (lowercase letters, digits and _)"
            elif e:
                msg = "Id: " + e
            elif self.skin_exists(sid.get()):
                msg = "Id: the mod already has a skin %r" % sid.get()
            elif not name.get().strip():
                msg = "Name: the selection shows it; it cannot be empty"
            err.configure(text=msg)
            btn_ok.state(["disabled"] if msg else ["!disabled"])
            return not msg

        def on_monster(*_):
            res = self.resolve_monster(mon.get(), choices)
            st["res"] = res
            prefix = res[1] if res else None
            if res is None:
                hint.configure(text="")
            elif res[3] == "unknown":
                hint.configure(text="unknown monster: taken as a custom monster id; no textures will be extracted")
            elif res[3] == "custom":
                hint.configure(text="custom monster on %s; default costume %d (monster.json)" % (prefix, res[2]))
            else:
                hint.configure(text="")
            if prefix != st["prefix"]:   # outro monstro: os botoes de base com os retratos dele
                st["prefix"] = prefix
                for w in rb_fr.winfo_children():
                    w.destroy()
                if res and res[3] != "unknown":
                    base.set(lock[2] if lock else res[2])
                    st["radios"] = self.costume_radios(rb_fr, base, prefix, enabled={lock[2]} if lock else None)
                else:
                    st["radios"] = []
                    ttk.Label(rb_fr, text="(none: the game uses the custom monster's costume)" if res else "",
                              style="Dim.TLabel").grid(row=0, column=0, sticky="w")
            if st["auto_name"]:
                set_quiet(name, default_name())
            validate()

        def on_name(*_):
            if st["guard"]:
                return
            st["auto_name"] = False
            if st["auto_id"]:
                set_quiet(sid, modkit.skin_slug(name.get()))
            validate()

        def on_id(*_):
            if st["guard"]:
                return
            st["auto_id"] = False
            if st["auto_name"]:
                set_quiet(name, default_name())
            validate()

        def ok():
            if not validate():
                return
            key, prefix, co, kind = st["res"]
            b = None if kind == "unknown" else base.get()
            if kind == "custom" and b == co:
                b = None   # o skin.json fica sem base: vale o costume do monster.json
            d.destroy()
            on_ok(key, sid.get(), b, name.get().strip().upper(), ai.get())

        btn_ok.configure(command=ok)
        set_quiet(sid, self.free_skin_id())
        mon.trace_add("write", on_monster)
        name.trace_add("write", on_name)
        sid.trace_add("write", on_id)
        if lock:
            mon.set(next((c[0] for c in choices if c[4] and c[1] == lock[0]), lock[0]))
        elif preset:
            mon.set(next((c[0] for c in choices if not c[4] and c[1] == preset), preset))
        else:
            mon.set(choices[0][0] if choices else "")
        d.bind("<Return>", lambda e: ok())
        d.bind("<Escape>", lambda e: d.destroy())
        self._skin_dlg = dict(d=d, mon=mon, base=base, name=name, sid=sid, ai=ai, ok=ok, err=err, hint=hint, state=st)   # para testes
        try:
            d.grab_set()
        except tk.TclError:
            pass
        return d

    def new_skin(self, preset=None):
        work, mod = self.work(), self.cur_mod()
        if not work:
            messagebox.showwarning("New skin", "Pick or create a mod first (\"Mod\" at the top).", parent=self.root)
            return

        def ok(key, sid, base, name, ai):
            self.run("Creating skin %s (%s)..." % (sid, key),
                     lambda disc=self.disc.get(), mods=self.mods.get(): modkit.skin_new(disc, work, mods, mod, key, sid, base, name, ai),
                     lambda: self.refresh_work(focus=["skins/" + sid]))

        self.skin_dialog("New skin", ok, preset=preset)

    def turn_selected_into_skin(self):
        key = self.selected_group()
        if key and self.costume_of_group(key):
            self.turn_into_skin(key)
        else:
            self.say("Select a monster costume group in the list first (e.g. MON/SHOGUN3).\n", "warn")

    def turn_into_skin(self, key):
        """Traje extraido (MON/SHOGUN3) vira skin: skin.json novo e os PNGs do grupo (todos, com o estado) para skins/<id>/."""
        c = self.costume_of_group(key)
        work, mod = self.work(), self.cur_mod()
        if not c or not work:
            return
        d, stem = key.rsplit("/", 1)

        def ok(mkey, sid, base, name, ai):
            def fn(disc=self.disc.get(), mods=self.mods.get()):
                modkit.skin_new(disc, work, mods, mod, mkey, sid, base, name, ai, textures=False)
                modkit.skin_adopt(work, d, stem, sid)

            self.run("Turning %s into skin %s..." % (key, sid), fn,
                     lambda: (self.refresh_work(focus=["skins/" + sid]), self.offer_remove_packed(mod, d, stem)))

        self.skin_dialog("Turn into skin", ok, lock=c)

    def offer_remove_packed(self, mod, d, stem):
        """Depois do Turn into skin: o que o mod ja tem empacotado desse traje (PNGs, .TEX/.RTX do --inject) continua trocando o
        traje original; avisa e oferece apagar."""
        folder = os.path.join(self.mods.get(), mod, *d.split("/"))
        hits = []
        for n in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
            m = modkit.PNG_RE.match(n)
            if (m and m.group(1).lower() == stem.lower()) or n.upper() in (stem.upper() + ".TEX", stem.upper() + ".RTX"):
                hits.append(os.path.join(folder, n))
        if not hits:
            return
        self.say("WARNING: %s/%s still has %d file(s) of %s built before (%s); they keep replacing the original costume in the "
                 "game.\n" % (mod, d, len(hits), stem.upper(), ", ".join(os.path.basename(p) for p in hits[:6]) +
                              (", ..." if len(hits) > 6 else "")), "warn")
        if not messagebox.askyesno("Turn into skin", "The mod already has %d file(s) of %s in %s/%s, built before; they still replace "
                                   "the original costume.\n\nDelete them from the mod? (the skin keeps its own copies)"
                                   % (len(hits), stem.upper(), mod, d), parent=self.root):
            return
        n = 0
        for p in hits:
            try:
                os.remove(p)
                n += 1
            except OSError as ex:
                self.say("%s\n" % ex, "err")
        self.say("Deleted %d file(s) of %s from %s/%s\n" % (n, stem.upper(), mod, d))

    def fill_skin_panel(self, sid):
        """Painel de propriedades da skin (Name, Base, CPUs may pick) com Save, que regrava o skin.json mantendo as outras chaves."""
        p = self.skin_panel
        for w in p.winfo_children():
            w.destroy()
        self._panel_sid = "skins/" + sid
        data = self.skin_data(sid)
        ttk.Label(p, text="Skin %s" % sid, style="Head.TLabel").grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 6))
        if not data:
            ttk.Label(p, text="No skin.json in %s" % modkit.skin_dir(self.mods.get(), self.cur_mod(), sid), style="Dim.TLabel",
                      wraplength=360).grid(row=1, column=0, columnspan=2, sticky="w")
            return
        mkey = str(data.get("monster", "")).lower()
        game = modkit.game_monster(mkey)
        custom = None if game else self._customs.get(mkey)
        prefix = modkit.MONSTERS[game] if game else (custom["prefix"] if custom else None)
        kind = "playable" if game else ("custom, %s" % custom["mod"] if custom else "custom monster the kit does not know")
        ttk.Label(p, text="Monster").grid(row=1, column=0, sticky="w", padx=(0, 8), pady=2)
        ttk.Label(p, text="%s  (%s)" % (data.get("monster", "?"), kind)).grid(row=1, column=1, sticky="w")
        name = tk.StringVar(value=data.get("name", ""))
        ttk.Label(p, text="Name").grid(row=2, column=0, sticky="w", padx=(0, 8), pady=2)
        ttk.Entry(p, textvariable=name, width=32).grid(row=2, column=1, sticky="ew", pady=2)
        b0 = data.get("base")
        base = tk.IntVar(value=b0 if isinstance(b0, int) and 0 <= b0 <= 3 else -1)
        ttk.Label(p, text="Base").grid(row=3, column=0, sticky="nw", padx=(0, 8), pady=(6, 2))
        rf = ttk.Frame(p)
        rf.grid(row=3, column=1, sticky="w", pady=(6, 2))
        extra = None
        if not game:   # custom: sem base vale o costume do monster.json
            extra = ("monster.json" + (" (%d)" % custom["costume"] if custom else ""), -1)
        elif base.get() < 0:
            base.set(0)
        self.costume_radios(rf, base, prefix, extra=extra)
        if not prefix:   # custom desconhecido: 0 a 3 sem retrato, o disco nao diz quais existem
            for w in rf.winfo_children():
                w.state(["!disabled"])
        ai = tk.BooleanVar(value=bool(data.get("ai")))
        ttk.Checkbutton(p, text="CPUs may pick this skin", variable=ai).grid(row=4, column=1, sticky="w", pady=(6, 0))
        ttk.Button(p, text="Save", style="Accent.TButton", command=lambda: self.save_skin_props(sid, name.get(), base.get(), ai.get())
                   ).grid(row=5, column=1, sticky="w", pady=(10, 0))
        p.columnconfigure(1, weight=1)
        self._skin_panel_vars = dict(name=name, base=base, ai=ai)   # para testes

    def save_skin_props(self, sid, name, base, ai):
        mod, mods = self.cur_mod(), self.mods.get()
        data = self.skin_data(sid)
        if not mod or not data:
            return
        if not name.strip():
            messagebox.showwarning("Skin", "The name cannot be empty (the selection shows it).", parent=self.root)
            return
        data["name"] = name.strip().upper()
        if base < 0:
            data.pop("base", None)
        else:
            data["base"] = base
        data["ai"] = bool(ai)
        try:
            modkit.write_skin_json(mods, mod, sid, data)
        except OSError as ex:
            self.say("Could not save the skin: %s\n" % ex, "err")
            return
        self.say("Skin %s saved: %s\n" % (sid, json.dumps(data)))
        self._panel_sid = None   # o painel relê o json
        self.refresh_work()
        gid = "grp:skins/" + sid
        if self.work_tree.exists(gid):
            self.work_tree.selection_set([gid])
        self.show_preview()

    def duplicate_skin(self, sid):
        work, mod = self.work(), self.cur_mod()
        if not work:
            return
        new = sid + "_copy"
        while True:
            new = simpledialog.askstring("Duplicate skin", "Id of the copy of %s (lowercase letters, digits and _):" % sid,
                                         initialvalue=new, parent=self.root)
            if new is None:
                return
            new = new.strip()
            e = modkit.skin_id_error(new) or ("the mod already has a skin %r" % new if self.skin_exists(new) else None)
            if not e:
                break
            messagebox.showwarning("Duplicate skin", "\"%s\": %s." % (new, e), parent=self.root)
        self.run("Duplicating skin %s as %s..." % (sid, new), lambda mods=self.mods.get(): modkit.skin_duplicate(mods, mod, work, sid, new),
                 lambda: self.refresh_work(focus=["skins/" + new]))

    def delete_skin(self, sid):
        work, mod = self.work(), self.cur_mod()
        if not work or not messagebox.askyesno("Delete skin", "Delete skin %s?\n\nIts folder in the mod (%s) and its PNGs in the work "
                                               "folder are deleted, edited or not." % (sid, modkit.skin_dir(self.mods.get(), mod, sid)),
                                               parent=self.root):
            return
        self.stop_sound()
        self.run("Deleting skin %s..." % sid, lambda mods=self.mods.get(): modkit.skin_delete(mods, mod, work, sid), self.refresh_work)

    # ------------------------------------------------------------ monstros custom (MODS.md, "Monstros custom")
    # (texto da lista, base do monster.json): os dez jogaveis e os chefes que o port deixa jogar (modkit.CUSTOM_BASES)
    BASES = [(n, next(g for g, p in modkit.MONSTERS.items() if p == pre)) for pre, n, _ in MONSTERS] + [
        ("Goliath Prime (boss)", "assboss"), ("Jelly (final boss, 2nd form)", "jelly"), ("Cerebulon (final boss)", "final")]
    BASE_HINTS = {
        "assboss": "Boss: no climbing animations (it holds the last pose); as a CPU it loads but does not show up (its AI needs the "
                   "points of its own level).",
        "jelly": "Boss: no rage, does not grapple; its special is the shock (Triangle + Circle).",
        "final": "Boss: does not pick up the special item.",
    }
    OWN_CAMERA = "(the base's own)"
    BASE_RULES = "(the base's)"

    def selected_monster(self):
        """Id do custom cujo grupo esta escolhido (sozinho); None senao."""
        key = self.selected_group()
        return key[9:] if key and key.startswith("monsters/") else None

    def monster_data(self, cid):
        """monster.json do custom na pasta do mod ({} sem ele)."""
        mod = self.cur_mod()
        return modkit._json_obj(os.path.join(modkit.monster_dir(self.mods.get(), mod, cid), "monster.json")) if mod else {}

    def monster_exists(self, cid):
        mod = self.cur_mod()
        return bool(mod) and (os.path.exists(modkit.monster_dir(self.mods.get(), mod, cid)) or
                              os.path.exists(os.path.join(self.work(), "monsters", cid)))

    def base_label(self, bname):
        return next((t for t, b in self.BASES if b == bname), bname or "")

    def camera_widgets(self, parent, row, cam_from, cam_scale):
        """Linha Camera: lista (a do proprio tipo ou a de outro monstro) e a escala em %."""
        ttk.Label(parent, text="Camera").grid(row=row, column=0, sticky="w", padx=(0, 8), pady=2)
        f = ttk.Frame(parent)
        f.grid(row=row, column=1, sticky="w", pady=2)
        box = ttk.Combobox(f, textvariable=cam_from, values=[self.OWN_CAMERA] + [t for t, _ in self.BASES], width=28, state="readonly")
        box.pack(side="left")
        ttk.Label(f, text="  distance").pack(side="left")
        sp = ttk.Spinbox(f, from_=10, to=1000, increment=10, textvariable=cam_scale, width=6)
        sp.pack(side="left", padx=(4, 0))
        ttk.Label(f, text="%").pack(side="left")
        return box, sp

    def camera_value(self, cam_from, cam_scale):
        """False (a do tipo), (base, escala) ou None se a escala nao serve."""
        if cam_from.get() == self.OWN_CAMERA:
            return False
        try:
            sc = int(cam_scale.get())
        except (ValueError, tk.TclError):
            return None
        b = next((b for t, b in self.BASES if t == cam_from.get()), None)
        return (b, sc) if b and 10 <= sc <= 1000 else None

    def monster_dialog(self, title, on_ok, lock=None):
        """Dialogo de monstro custom novo: Base, Costume, Name, Id, Camera, Narrator, texturas proprias, CPUs. lock = (base,
        prefixo, traje): base e traje travados (Turn into custom monster). on_ok(dict com cid, base, costume, name, ai, camera,
        narrator, textures)."""
        d = tk.Toplevel(self.root, bg=BG)
        d.title(title)
        d.transient(self.root)
        d.resizable(False, False)
        fr = ttk.Frame(d, padding=12)
        fr.pack(fill="both", expand=True)
        base, co, name, cid = tk.StringVar(), tk.IntVar(value=0), tk.StringVar(), tk.StringVar()
        cam_from, cam_scale, narr = tk.StringVar(value=self.OWN_CAMERA), tk.StringVar(value="100"), tk.StringVar()
        tex, ai = tk.BooleanVar(value=True), tk.BooleanVar(value=False)
        st = {"base": None, "auto_id": True, "guard": False}
        others = modkit.find_customs(self.mods.get())

        ttk.Label(fr, text="Base type").grid(row=0, column=0, sticky="w", padx=(0, 8), pady=2)
        ttk.Combobox(fr, textvariable=base, values=[t for t, _ in self.BASES], width=40,
                     state="disabled" if lock else "readonly").grid(row=0, column=1, sticky="ew", pady=2)
        hint = ttk.Label(fr, text="", style="Dim.TLabel", wraplength=460, justify="left")
        hint.grid(row=1, column=1, sticky="w")
        ttk.Label(fr, text="Costume").grid(row=2, column=0, sticky="nw", padx=(0, 8), pady=(8, 2))
        rb_fr = ttk.Frame(fr)
        rb_fr.grid(row=2, column=1, sticky="w", pady=(8, 2))
        ttk.Label(fr, text="Name").grid(row=3, column=0, sticky="w", padx=(0, 8), pady=2)
        ttk.Entry(fr, textvariable=name, width=40).grid(row=3, column=1, sticky="ew", pady=2)
        ttk.Label(fr, text="Id").grid(row=4, column=0, sticky="w", padx=(0, 8), pady=2)
        ttk.Entry(fr, textvariable=cid, width=40).grid(row=4, column=1, sticky="ew", pady=2)
        ttk.Label(fr, text="Folder name and what WOTM_MATCH takes: lowercase letters, digits and _, starting with a letter",
                  style="Dim.TLabel").grid(row=5, column=1, sticky="w")
        self.camera_widgets(fr, 6, cam_from, cam_scale)
        ttk.Label(fr, text="Narrator").grid(row=7, column=0, sticky="w", padx=(0, 8), pady=2)
        ttk.Combobox(fr, textvariable=narr, values=[""] + modkit.list_narrators(self.mods.get()), width=28).grid(
            row=7, column=1, sticky="w", pady=2)
        ttk.Label(fr, text="narrator/<name>.mp3 said when it is picked; empty = the base's", style="Dim.TLabel").grid(
            row=8, column=1, sticky="w")
        if not lock:
            ttk.Checkbutton(fr, text="Own look: extract the costume's textures to edit (only this monster uses them)",
                            variable=tex).grid(row=9, column=1, sticky="w", pady=(6, 0))
        ttk.Checkbutton(fr, text="CPUs may pick this monster", variable=ai).grid(row=10, column=1, sticky="w", pady=(4, 0))
        err = ttk.Label(fr, text="", style="Err.TLabel", wraplength=460, justify="left")
        err.grid(row=11, column=0, columnspan=2, sticky="w", pady=(6, 0))
        bf = ttk.Frame(fr)
        bf.grid(row=12, column=0, columnspan=2, sticky="e", pady=(10, 0))
        btn_ok = ttk.Button(bf, text="OK", style="Accent.TButton")
        btn_ok.pack(side="left")
        ttk.Button(bf, text="Cancel", command=d.destroy).pack(side="left", padx=(6, 0))
        fr.columnconfigure(1, weight=1)

        def validate(*_):
            msg = ""
            e = modkit.custom_id_error(cid.get())
            if not st["base"]:
                msg = "Base type: pick one"
            elif e:
                msg = "Id: " + e
            elif self.monster_exists(cid.get()):
                msg = "Id: the mod already has a monster %r" % cid.get()
            elif not name.get().strip():
                msg = "Name: the selection shows it; it cannot be empty"
            elif self.camera_value(cam_from, cam_scale) is None:
                msg = "Camera: the distance is 10 to 1000 %"
            note = ("note: mod %s also has a monster %r; the last mod in alphabetical order wins" % (others[cid.get()]["mod"], cid.get())
                    if cid.get() in others else "")
            err.configure(text=msg or note)
            btn_ok.state(["disabled"] if msg else ["!disabled"])
            return not msg

        def on_base(*_):
            b = next((b for t, b in self.BASES if t == base.get()), None)
            if b == st["base"]:
                return
            st["base"] = b
            prefix = modkit.base_prefix(b) if b else None
            dco, dcam = modkit.BOSS_DEFAULTS.get(b, (0, None))
            co.set(lock[2] if lock else dco)
            for w in rb_fr.winfo_children():
                w.destroy()
            if prefix:
                self.costume_radios(rb_fr, co, prefix, enabled={lock[2]} if lock else None)
            cam_from.set(self.base_label(dcam[0]) if dcam else self.OWN_CAMERA)
            cam_scale.set(str(dcam[1]) if dcam else "100")
            hint.configure(text=self.BASE_HINTS.get(b, "Runs the code and the AI of %s. To change only its look, a skin is enough."
                                                    % base.get()))
            validate()

        def on_name(*_):
            if not st["guard"] and st["auto_id"]:
                s = modkit.skin_slug(name.get())
                st["guard"] = True
                cid.set(("m_" + s if s[:1].isdigit() else s)[:32])
                st["guard"] = False
            validate()

        def on_id(*_):
            if not st["guard"]:
                st["auto_id"] = False
            validate()

        def ok():
            if not validate():
                return
            v = dict(cid=cid.get(), base=st["base"], costume=co.get(), name=name.get().strip().upper(), ai=ai.get(),
                     camera=self.camera_value(cam_from, cam_scale), narrator=narr.get().strip().lower() or None,
                     textures=bool(tex.get()) and not lock)
            d.destroy()
            on_ok(v)

        btn_ok.configure(command=ok)
        base.trace_add("write", on_base)
        name.trace_add("write", on_name)
        cid.trace_add("write", on_id)
        for v in (cam_from, cam_scale):
            v.trace_add("write", validate)
        base.set(self.base_label(lock[0]) if lock else self.BASES[0][0])
        n = next((i for i in range(1, 1000) if not self.monster_exists("monster_%d" % i)), 1)
        name.set("MONSTER %d" % n)
        d.bind("<Return>", lambda e: ok())
        d.bind("<Escape>", lambda e: d.destroy())
        self._monster_dlg = dict(d=d, base=base, co=co, name=name, cid=cid, cam_from=cam_from, cam_scale=cam_scale, narr=narr,
                                 tex=tex, ai=ai, ok=ok, err=err, hint=hint)   # para testes
        try:
            d.grab_set()
        except tk.TclError:
            pass
        return d

    def new_monster(self):
        work, mod = self.work(), self.cur_mod()
        if not work:
            messagebox.showwarning("New monster", "Pick or create a mod first (\"Mod\" at the top).", parent=self.root)
            return

        def ok(v):
            self.run("Creating custom monster %s (base %s)..." % (v["cid"], v["base"]),
                     lambda disc=self.disc.get(), mods=self.mods.get(): modkit.monster_new(
                         disc, work, mods, mod, v["cid"], v["base"], v["costume"], v["name"], v["ai"], v["camera"], v["narrator"],
                         v["textures"]),
                     lambda: self.refresh_work(focus=["monsters/" + v["cid"]]))

        self.monster_dialog("New custom monster", ok)

    def turn_into_monster(self, key):
        """Traje extraido (MON/LIZARD2, MON/ASSBOSS1) vira monstro custom: monster.json novo ("files": true) e os PNGs do grupo
        (todos, com o estado) para monsters/<id>/files/."""
        c = self.costume_of_group(key, bosses=True)
        work, mod = self.work(), self.cur_mod()
        if not c or not work:
            return
        d, stem = key.rsplit("/", 1)

        def ok(v):
            def fn(disc=self.disc.get(), mods=self.mods.get()):
                modkit.monster_new(disc, work, mods, mod, v["cid"], v["base"], v["costume"], v["name"], v["ai"], v["camera"],
                                   v["narrator"], textures=False, files=True)
                modkit.skin_adopt(work, d, stem, None, to="monsters/%s/files" % v["cid"])

            self.run("Turning %s into custom monster %s..." % (key, v["cid"]), fn,
                     lambda: (self.refresh_work(focus=["monsters/" + v["cid"]]), self.offer_remove_packed(mod, d, stem)))

        self.monster_dialog("Turn into custom monster", ok, lock=c)

    def fill_monster_panel(self, cid):
        """Painel de propriedades do custom (Name, Camera, Narrator, Rules, CPUs, retrato do HUD, modelo proprio) com Save, que
        regrava o monster.json mantendo as outras chaves."""
        p = self.skin_panel
        for w in p.winfo_children():
            w.destroy()
        self._panel_sid = "monsters/" + cid
        mods, mod = self.mods.get(), self.cur_mod()
        data = self.monster_data(cid)
        mdir = modkit.monster_dir(mods, mod, cid)
        ttk.Label(p, text="Custom monster %s" % cid, style="Head.TLabel").grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 6))
        if not data:
            ttk.Label(p, text="No monster.json in %s" % mdir, style="Dim.TLabel", wraplength=360).grid(row=1, column=0, columnspan=2, sticky="w")
            return
        bname = modkit.base_name(str(data.get("base", "")))
        ttk.Label(p, text="Base").grid(row=1, column=0, sticky="w", padx=(0, 8), pady=2)
        ttk.Label(p, text="%s, costume %s" % (self.base_label(bname) if bname else "%r (not accepted by the port)" % data.get("base"),
                                              data.get("costume", 0))).grid(row=1, column=1, sticky="w")
        name = tk.StringVar(value=data.get("name", ""))
        ttk.Label(p, text="Name").grid(row=2, column=0, sticky="w", padx=(0, 8), pady=2)
        ttk.Entry(p, textvariable=name, width=32).grid(row=2, column=1, sticky="ew", pady=2)
        cam = data.get("camera") if isinstance(data.get("camera"), dict) else {}
        cb = modkit.base_name(str(cam.get("from", ""))) if cam else None
        cam_from = tk.StringVar(value=self.base_label(cb) if cb else self.OWN_CAMERA)
        cam_scale = tk.StringVar(value=str(cam.get("scale", 100)) if cam else "100")
        self.camera_widgets(p, 3, cam_from, cam_scale)
        narr = tk.StringVar(value=data.get("narrator", "") if isinstance(data.get("narrator"), str) else "")
        ttk.Label(p, text="Narrator").grid(row=4, column=0, sticky="w", padx=(0, 8), pady=2)
        ttk.Combobox(p, textvariable=narr, values=[""] + modkit.list_narrators(mods), width=28).grid(row=4, column=1, sticky="w", pady=2)
        kind = tk.StringVar(value=data.get("kind") or self.BASE_RULES)
        ttk.Label(p, text="Rules").grid(row=5, column=0, sticky="w", padx=(0, 8), pady=2)
        ttk.Combobox(p, textvariable=kind, values=[self.BASE_RULES] + list(modkit.KINDS), width=28, state="readonly").grid(
            row=5, column=1, sticky="w", pady=2)
        ai = tk.BooleanVar(value=bool(data.get("ai")))
        ttk.Checkbutton(p, text="CPUs may pick this monster", variable=ai).grid(row=6, column=1, sticky="w", pady=(4, 0))
        # retrato do HUD
        ttk.Label(p, text="HUD portrait").grid(row=7, column=0, sticky="nw", padx=(0, 8), pady=(8, 2))
        pf = ttk.Frame(p)
        pf.grid(row=7, column=1, sticky="w", pady=(8, 2))
        por = data.get("portrait") if isinstance(data.get("portrait"), str) else ""
        pimg = os.path.join(mdir, por) if por else ""
        self._portrait_img = None
        if pimg and os.path.isfile(pimg) and Image is not None:
            try:
                im = Image.open(pimg).convert("RGBA")
                im.thumbnail((128, 64))
                self._portrait_img = ImageTk.PhotoImage(im)
            except Exception:
                pass
        ttk.Label(pf, image=self._portrait_img or "", text="" if self._portrait_img else (por + " (not found)" if por else "the costume's"),
                  style="Dim.TLabel").pack(side="left")
        ttk.Button(pf, text="Set...", command=lambda: self.set_portrait(cid)).pack(side="left", padx=(8, 0))
        if por:
            ttk.Button(pf, text="Remove", command=lambda: self.remove_portrait(cid)).pack(side="left", padx=(4, 0))
        # aparencia: PNGs proprios e modelo proprio
        ttk.Label(p, text="Look").grid(row=8, column=0, sticky="nw", padx=(0, 8), pady=(8, 2))
        lf = ttk.Frame(p)
        lf.grid(row=8, column=1, sticky="w", pady=(8, 2))
        wfiles = os.path.join(self.work(), "monsters", cid, "files")
        names = sorted(os.listdir(wfiles)) if os.path.isdir(wfiles) else []
        npng = sum(1 for n in names if modkit.PNG_RE.match(n))
        model = [n for n in names if os.path.splitext(n)[1].upper() in (".NGP", ".PTR", ".TEX", ".RTX")]
        if not data.get("files"):
            look = "the disc costume (no own files)"
        else:
            look = "%d texture(s) to edit in monsters/%s/files" % (npng, cid) + ("; own model: %s" % ", ".join(model) if model else "")
        ttk.Label(lf, text=look, style="Dim.TLabel", wraplength=300, justify="left").pack(anchor="w")
        ttk.Button(lf, text="Import model files...", command=lambda: self.import_model(cid)).pack(anchor="w", pady=(4, 0))
        bf = ttk.Frame(p)
        bf.grid(row=9, column=1, sticky="w", pady=(10, 0))
        ttk.Button(bf, text="Save", style="Accent.TButton",
                   command=lambda: self.save_monster_props(cid, name.get(), cam_from, cam_scale, narr.get(), kind.get(), ai.get())).pack(side="left")
        ttk.Button(bf, text="New skin for it...", command=lambda: self.new_skin(preset=cid)).pack(side="left", padx=(6, 0))
        p.columnconfigure(1, weight=1)
        self._monster_panel_vars = dict(name=name, cam_from=cam_from, cam_scale=cam_scale, narr=narr, kind=kind, ai=ai)   # para testes

    def reselect(self, key):
        """Depois de mudar o monster.json/skin.json: o painel rele e o grupo continua escolhido."""
        self._panel_sid = None
        self.refresh_work()
        gid = "grp:" + key
        if self.work_tree.exists(gid):
            self.work_tree.selection_set([gid])
        self.show_preview()

    def save_monster_props(self, cid, name, cam_from, cam_scale, narrator, kind, ai):
        mod, mods = self.cur_mod(), self.mods.get()
        data = self.monster_data(cid)
        if not mod or not data:
            return
        if not name.strip():
            messagebox.showwarning("Monster", "The name cannot be empty (the selection shows it).", parent=self.root)
            return
        cam = self.camera_value(cam_from, cam_scale)
        if cam is None:
            messagebox.showwarning("Monster", "Camera: the distance is 10 to 1000 %.", parent=self.root)
            return
        data["name"] = name.strip().upper()
        if cam:
            data["camera"] = {"from": cam[0], "scale": cam[1]}
        else:
            data.pop("camera", None)
        for k, v in (("narrator", narrator.strip().lower()), ("kind", "" if kind == self.BASE_RULES else kind)):
            if v:
                data[k] = v
            else:
                data.pop(k, None)
        data["ai"] = bool(ai)
        try:
            modkit.write_monster_json(mods, mod, cid, data)
        except OSError as ex:
            self.say("Could not save the monster: %s\n" % ex, "err")
            return
        self.say("Monster %s saved: %s\n" % (cid, json.dumps(data)))
        self.reselect("monsters/" + cid)

    def set_portrait(self, cid):
        work, mod = self.work(), self.cur_mod()
        if not work:
            return
        p = filedialog.askopenfilename(parent=self.root, title="HUD portrait of %s (2:1, e.g. 128x64 or 512x256)" % cid,
                                       filetypes=[("Images", " ".join("*" + e for e in self.IMG_EXT)), ("All files", "*.*")])
        if not p:
            return
        src = p
        if not p.lower().endswith(".png"):   # outro formato: vira PNG
            if Image is None:
                messagebox.showwarning("HUD portrait", "Without Pillow (pip install pillow) only PNG is accepted.", parent=self.root)
                return
            try:
                src = os.path.join(tempfile.mkdtemp(prefix="wotm_portrait_"), "portrait.png")
                Image.open(p).convert("RGBA").save(src)
            except Exception as ex:
                self.say("Could not open %s: %s\n" % (p, ex), "err")
                return
        self.run("Setting the HUD portrait of %s..." % cid,
                 lambda disc=self.disc.get(), mods=self.mods.get(): modkit.monster_portrait(disc, work, mods, mod, cid, src),
                 lambda: self.reselect("monsters/" + cid))

    def remove_portrait(self, cid):
        mods, mod, work = self.mods.get(), self.cur_mod(), self.work()
        data = self.monster_data(cid)
        por = data.pop("portrait", None)
        data.pop("portraitTex", None)
        if not mod or not por:
            return
        modkit.write_monster_json(mods, mod, cid, data)
        for f in (os.path.join(modkit.monster_dir(mods, mod, cid), por), os.path.join(work, "monsters", cid, por)):
            try:
                os.remove(f)
            except OSError:
                pass
        st = modkit.load_state(work)
        st["files"].pop("monsters/%s/%s" % (cid, por), None)
        modkit.save_state(work, st)
        self.say("Monster %s: HUD portrait removed (back to the costume's)\n" % cid)
        self.reselect("monsters/" + cid)

    def import_model(self, cid):
        work, mod = self.work(), self.cur_mod()
        if not work:
            return
        ps = filedialog.askopenfilenames(parent=self.root, title="Model of %s: .NGP and .PTR (and .TEX/.RTX, optional)" % cid,
                                         filetypes=[("Model files", "*.NGP *.PTR *.TEX *.RTX *.ngp *.ptr *.tex *.rtx"), ("All files", "*.*")])
        if not ps:
            return
        self.run("Importing the model of %s..." % cid,
                 lambda mods=self.mods.get(): modkit.monster_model(work, mods, mod, cid, list(ps)),
                 lambda: self.reselect("monsters/" + cid))

    def duplicate_monster(self, cid):
        work, mod = self.work(), self.cur_mod()
        if not work:
            return
        new = cid + "_copy"
        while True:
            new = simpledialog.askstring("Duplicate monster", "Id of the copy of %s (lowercase letters, digits and _):" % cid,
                                         initialvalue=new, parent=self.root)
            if new is None:
                return
            new = new.strip()
            e = modkit.custom_id_error(new) or ("the mod already has a monster %r" % new if self.monster_exists(new) else None)
            if not e:
                break
            messagebox.showwarning("Duplicate monster", "\"%s\": %s." % (new, e), parent=self.root)
        self.run("Duplicating monster %s as %s..." % (cid, new),
                 lambda mods=self.mods.get(): modkit.monster_duplicate(mods, mod, work, cid, new),
                 lambda: self.refresh_work(focus=["monsters/" + new]))

    def delete_monster(self, cid):
        work, mod = self.work(), self.cur_mod()
        if not work or not messagebox.askyesno("Delete monster", "Delete custom monster %s?\n\nIts folder in the mod (%s) and its "
                                               "files in the work folder are deleted, edited or not." %
                                               (cid, modkit.monster_dir(self.mods.get(), mod, cid)), parent=self.root):
            return
        self.stop_sound()
        self.run("Deleting monster %s..." % cid, lambda mods=self.mods.get(): modkit.monster_delete(mods, mod, work, cid),
                 self.refresh_work)

    # ------------------------------------------------------------ mod
    def mod_names(self):
        m = self.mods.get()
        try:
            return sorted((n for n in os.listdir(m) if os.path.isdir(os.path.join(m, n)) and self.valid_mod_name(n)), key=str.lower)
        except OSError:
            return []

    def refresh_mods(self):
        """Lista os mods em <mods>; o mod atual que nao existe mais sai (sem mod: lista vazia, Extract/Build desligados)."""
        names = self.mod_names()
        self.mod_box["values"] = names
        if self.mod.get() and self.mod.get() not in names:
            self.mod.set("")
        self.update_mod_state()

    def build_mod(self):
        mod, work = self.cur_mod(), self.work()
        if not work:
            messagebox.showwarning("Build mod", "Pick or create a mod first (\"Mod\" at the top).", parent=self.root)
            return
        self.run("Building %s..." % mod, lambda disc=self.disc.get(), mods=self.mods.get(): modkit.pack(disc, work, mods, mod),
                 lambda: (self.refresh_mods(), self.refresh_work(),
                          self.say("Done. Start the game: the log shows a [mods] line for each file and texture of the mod.\n")))

    def check(self):
        self.run("Checking...", lambda disc=self.disc.get(), mods=self.mods.get(): modkit.check(disc, mods))

    # ------------------------------------------------------------ execucao em segundo plano
    def task_buttons(self):
        """Botoes desligados enquanto uma tarefa roda."""
        return [self.btn_check] + [b for b in self.mod_buttons if b is not self.btn_open_work]

    def run(self, title, fn, done=None):
        if self.busy:
            self.say("Still working on the previous task; try again when it finishes.\n", "warn")
            return
        self.busy = True
        for b in self.task_buttons():
            b.state(["disabled"])
        self.say("\n== %s\n" % title)
        w = QueueWriter(self.log_q)

        def body():
            try:
                with redirect_stdout(w), redirect_stderr(w):
                    fn()
            except SystemExit as ex:
                self.log_q.put(("err", "%s\n" % ex))
            except Exception:
                self.log_q.put(("err", traceback.format_exc()))
            self.log_q.put(("done", done))

        threading.Thread(target=body, daemon=True).start()

    def pump_log(self):
        self.pump_portraits()
        try:
            while True:
                item = self.log_q.get_nowait()
                if isinstance(item, tuple):
                    kind, val = item
                    if kind == "done":
                        self.busy = False
                        self.btn_check.state(["!disabled"])
                        self.update_mod_state()   # os do mod so voltam se ha mod
                        if val:
                            val()
                        continue
                    self.say(val, "err")
                else:
                    self.say(item, "warn" if ("WARNING" in item or "ERROR" in item) else None)
        except queue.Empty:
            pass
        self.root.after(100, self.pump_log)

    def say(self, s, tag=None):
        self.log.insert("end", s, tag or ())
        self.log.see("end")


def main():
    root = tk.Tk()
    try:
        ico = os.path.join(modkit.REPO, "native", "wotm.ico")
        if os.path.exists(ico):
            root.iconbitmap(ico)
    except tk.TclError:
        pass
    app = App(root)
    # com pythonw nao ha console: erro dentro da janela vai para o log dela
    root.report_callback_exception = lambda *a: app.say("Error: %s\n" % "".join(traceback.format_exception(*a)), "err")
    root.mainloop()


if __name__ == "__main__":
    main()
