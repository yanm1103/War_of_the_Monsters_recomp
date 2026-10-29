// Launcher do jogo ao vivo: janela com caixas de marcar para as flags de ambiente (WOTM_*, PS2X_*) e um botao que roda o
// run_live.ps1 do runtime com elas. Para adicionar uma flag, acrescente uma linha em Flags. O estado fica em WotM_Launcher.ini,
// ao lado do executavel. Compilar: tools\launcher\build.bat (csc do .NET Framework, C# 5).
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

class Flag
{
    public string Group, Key, Label, Tip;
    public bool Default;
    public Flag(string group, string key, bool def, string label, string tip)
    {
        Group = group; Key = key; Default = def; Label = label; Tip = tip;
    }
}

// GroupBox com borda e titulo desenhados aqui no tema escuro: com estilo visual o titulo sai na cor do tema e ignora ForeColor.
class DarkGroupBox : GroupBox
{
    protected override void OnPaint(PaintEventArgs e)
    {
        if (!Launcher.Dark) { base.OnPaint(e); return; }
        Graphics g = e.Graphics;
        Size sz = TextRenderer.MeasureText(g, Text, Font);
        int y = sz.Height / 2;
        g.Clear(BackColor);
        using (var pen = new Pen(Color.FromArgb(85, 85, 85))) g.DrawRectangle(pen, 0, y, Width - 1, Height - y - 1);
        var r = new Rectangle(7, 0, sz.Width, sz.Height);
        using (var b = new SolidBrush(BackColor)) g.FillRectangle(b, r);
        TextRenderer.DrawText(g, Text, Font, r, ForeColor);
    }
}

// CheckBox desenhada aqui no tema escuro (caixa escura; marcada, azul com o visto branco): o desenho do Windows nao tem versao escura
// que respeite a cor do texto.
class DarkCheckBox : CheckBox
{
    bool hover;
    protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }

    protected override void OnPaint(PaintEventArgs e)
    {
        if (!Launcher.Dark) { base.OnPaint(e); return; }
        Graphics g = e.Graphics;
        g.Clear(BackColor);
        const int box = 14;
        var r = new Rectangle(0, (Height - box) / 2, box, box);
        Color fill, border;
        if (Checked)
        {
            fill = Enabled ? (hover ? Color.FromArgb(120, 185, 255) : Color.FromArgb(96, 165, 250)) : Color.FromArgb(70, 90, 115);
            border = fill;
        }
        else
        {
            fill = Color.FromArgb(45, 45, 45);
            border = !Enabled ? Color.FromArgb(75, 75, 75) : (hover ? Color.FromArgb(200, 200, 200) : Color.FromArgb(150, 150, 150));
        }
        g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
        using (var b = new SolidBrush(fill)) g.FillRectangle(b, r);
        using (var p = new Pen(border)) g.DrawRectangle(p, r.X, r.Y, box - 1, box - 1);
        if (Checked)
            using (var p = new Pen(Enabled ? Color.FromArgb(20, 20, 20) : Color.FromArgb(150, 150, 150), 2f))
                g.DrawLines(p, new[] { new PointF(r.X + 3f, r.Y + 7f), new PointF(r.X + 6f, r.Y + 10f), new PointF(r.X + 11f, r.Y + 4f) });
        g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.Default;
        var tr = new Rectangle(box + 4, 0, Width - box - 4, Height);
        TextRenderer.DrawText(g, Text, Font, tr, Enabled ? ForeColor : Color.FromArgb(120, 120, 120),
            TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
    }
}

class Launcher : Form
{
    // Chaves com ':' sao parametros do run_live.ps1; as outras viram variaveis de ambiente (=1 quando marcadas, removidas quando nao).
    // Diagnosticos sem caixa continuam valendo pela variavel (WOTM_RATEMIN=0, WOTM_VU0_INTERP, PS2X_PAD_DEADZONE; tabela no NATIVE.md).
    // Caixas de investigacoes fechadas sairam (buracos nos monstros: NOSKIN/NOVCOL/OPAQUE/ONEPASS; giro sozinho: TURNLOG/PS2X_PADLOG).
    static readonly Flag[] Flags = {
        new Flag("Run", ":Auto", false, "Scripted input up to a stage", "-Auto 1: a scripted pad plays up to a stage (~50 s). Unchecked: you play with the keyboard."),
        new Flag("Run", "WOTM_FOTOS", false, "Debug photos (F7 pauses, F6 steps, F8 photo with a note)", "F8 saves the frame (a VU1 vs native check snapshot, ~40 MB), a PNG of the window and your note in <runtime>\\fotos\\<date>. Delete the photos once the issue is fixed."),
        new Flag("Run", "WOTM_AIVSAI", false, "Player controlled by AI", "Player 1's monster becomes an AI (playerInit -> aiInit): AI vs AI fight. For batches without the menu use tools\\lote.ps1."),
        new Flag("Run", "WOTM_WATCHDOG=0", false, "Turn off the freeze watchdog", "By default, g_frame stuck for 8 s (outside movies) writes [vigia] to live.err.log and ram_/vram_vigia_<g_frame>.bin to discroot; game frames below 25/s log a [vigia] LENTO warning."),
        new Flag("Run", "WOTM_FMV_MUTE", false, "Mute movies", ""),

        new Flag("Rendering diagnostics", "WOTM_NOFX", false, "No particles/effects", "Turns off the fx[] particles and the wotm_fx effects."),
        new Flag("Rendering diagnostics", "WOTM_NOSHADOW", false, "No shadows", "Turns off blocker shadows (monsters and the like)."),

        new Flag("Logs (live.err.log)", ":Stats", false, "Statistics", "-Stats 1: [native]/[ui]/[time] lines."),
        new Flag("Logs (live.err.log)", "WOTM_FIELDLOG", false, "Fields per frame", "One [field] line per game frame: fields, updateRate, EE cycles, host time and the most expensive loops."),
        new Flag("Logs (live.err.log)", "WOTM_CAMLOG", false, "Camera on every draw", "One [cam] line per draw: t, g_frame, fov, position and direction."),
        new Flag("Logs (live.err.log)", "PS2X_SND_LOG", false, "Native sound", "989snd calls."),
    };

    readonly Dictionary<Flag, CheckBox> boxes = new Dictionary<Flag, CheckBox>();
    readonly string exeDir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
    string IniPath { get { return Path.Combine(exeDir, "WotM_Launcher.ini"); } }
    readonly Label status = new Label();

    Launcher()
    {
        Text = "WotM - options";
        try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch (Exception) { }   // o do exe (native\wotm.ico)
        Font = new Font("Segoe UI", 9f);
        AutoSize = true;
        AutoSizeMode = AutoSizeMode.GrowAndShrink;
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;

        var tips = new ToolTip { AutoPopDelay = 30000 };
        var root = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, Padding = new Padding(10), WrapContents = false };
        // duas colunas: partida e trapacas a esquerda; as caixas de Flags a direita
        var cols = new FlowLayoutPanel { FlowDirection = FlowDirection.LeftToRight, AutoSize = true, WrapContents = false, Margin = new Padding(0) };
        var left = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, WrapContents = false };
        var right = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, WrapContents = false };
        cols.Controls.Add(left);
        cols.Controls.Add(right);
        root.Controls.Add(cols);
        Dictionary<string, string> saved = LoadIni();
        left.Controls.Add(MatchGroup(saved, tips));
        left.Controls.Add(CheatsGroup(saved, tips));
        GroupBox gb = null;
        FlowLayoutPanel inner = null;
        foreach (Flag f in Flags)
        {
            if (gb == null || gb.Text != f.Group)
            {
                gb = new DarkGroupBox { Text = f.Group, AutoSize = true, Width = 420, Padding = new Padding(8) };
                inner = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, Dock = DockStyle.Fill, WrapContents = false };
                gb.Controls.Add(inner);
                right.Controls.Add(gb);
            }
            var cb = new DarkCheckBox { Text = f.Label, AutoSize = true };
            string v;
            cb.Checked = saved.TryGetValue(f.Key, out v) ? v == "1" : f.Default;
            if (f.Tip.Length > 0) tips.SetToolTip(cb, f.Tip);
            inner.Controls.Add(cb);
            boxes[f] = cb;
        }
        UpdateMatch();
        int wide = 0;
        foreach (Control c in right.Controls) wide = Math.Max(wide, c.PreferredSize.Width);
        foreach (Control c in right.Controls) c.MinimumSize = new Size(wide, 0);

        var buttons = new FlowLayoutPanel { AutoSize = true };
        var play = new Button { Text = "Play", AutoSize = true };
        play.Click += delegate { Play(); };
        var log = new Button { Text = "Open live.err.log", AutoSize = true };
        log.Click += delegate { OpenLog(); };
        var reset = new Button { Text = "Defaults", AutoSize = true };
        reset.Click += delegate
        {
            foreach (var kv in boxes) kv.Value.Checked = kv.Key.Default;
            matchOn.Checked = false;
            matchPlayer.SelectedIndex = 0;
            matchPlayerSkin.SelectedIndex = 0;
            matchLevel.SelectedIndex = 0;
            for (int i = 0; i < matchAIs.Length; ++i) { matchAIs[i].SelectedIndex = i == 0 ? 1 : 0; matchAISkins[i].SelectedIndex = 0; }
            foreach (CheckBox c in cheatBoxes) c.Checked = false;
            cheatStamina.SelectedIndex = 0;
            cheatWin.Value = 0;
        };
        buttons.Controls.Add(play);
        buttons.Controls.Add(log);
        var modkit = new Button { Text = "Mod kit", AutoSize = true };
        modkit.Click += delegate { OpenModKit(); };
        tips.SetToolTip(modkit, "Extract textures and sounds from the disc, edit them and pack them into mods\\<mod>\\ (docs\\guides\\MODS.md).");
        buttons.Controls.Add(modkit);
        buttons.Controls.Add(reset);
        root.Controls.Add(buttons);
        status.AutoSize = true;
        status.MaximumSize = new Size(420, 0);
        root.Controls.Add(status);
        Controls.Add(root);
        if (Dark) ApplyDark(this);
        AcceptButton = play;
        FormClosing += delegate { SaveIni(); };
    }

    // Partida direta (WOTM_MATCH=<jogador>[:<skin>][,<ia>[:<skin>]...],<fase>; sem IA = sozinho na fase): indices do GLOSSARY.md. O runtime
    // aceita ate 7 IAs (8 monstros). Skin = figurino 0..3 (arquivos <PREFIXO>0-3 em disc\MON); "Random" (monstro ou skin) e sorteado no Play.
    // Fora da lista: Spider (6) e Ogre (12), cortados, e os chefes (13..15).
    static readonly int[] MonsterIds = { 1, 2, 3, 4, 5, 7, 8, 9, 10, 11 };
    static readonly string[] MonsterNames = { "Congar", "Robo-47", "Togera", "Ultra V", "Preytor", "Kineticlops", "Magmo", "Raptros", "Agamo", "Zorgulon" };
    static readonly int[] LevelIds = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15, 16 };
    static readonly string[] LevelNames = { "Midtown Park", "Gamblers Gulch", "Rosdale Canyon", "Metro City", "Century Airfield", "Atomic Island",
        "Baytown", "Club Caldera", "Tsunopolis", "UFO", "Capitol", "Lake Powell", "Mini Baytown", "Volcano", "Tokyo Deathmatch" };
    const int MatchMaxAIs = 7;
    const int RandomId = -1;   // id salvo no ini para "Random" (monstro e skin)
    static readonly Random rng = new Random();
    // Nome do jogo e prefixo dos arquivos em MON\ de cada monstro de MonsterIds (o skin.json aceita os dois)
    static readonly string[] MonsterGameNames = { "congar", "robo-47", "togera", "ultra-v", "preytor", "kineticlops", "magmo", "raptros", "agamo", "zorgulon" };
    static readonly string[] MonsterPrefixes = { "MONKEY", "ROBOT", "LIZARD", "SHOGUN", "MANTIS", "ENERGY", "LAVA", "DRAGON", "ROCK", "ALIEN" };

    readonly CheckBox matchOn = new DarkCheckBox { Text = "Direct match, no menu", AutoSize = true };
    readonly ComboBox matchPlayer = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 160 };
    readonly ComboBox matchLevel = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 160 };
    readonly ComboBox[] matchAIs = new ComboBox[MatchMaxAIs];
    readonly ComboBox matchPlayerSkin = SkinBox(120);
    readonly ComboBox[] matchAISkins = new ComboBox[MatchMaxAIs];

    // Item da caixa de skin: o texto e o valor do WOTM_MATCH ("0".."3", o id da skin de mod, "random")
    class SkinItem
    {
        public string Label, Value;
        public SkinItem(string label, string value) { Label = label; Value = value; }
        public override string ToString() { return Label; }
    }
    const string RandomSkin = "random";

    // Skins de mod (discroot\mods\<mod>\skins\<pasta>\skin.json, MODS.md) por indice em MonsterIds: (nome, id)
    readonly List<KeyValuePair<string, string>>[] modSkins = new List<KeyValuePair<string, string>>[10];

    static string Norm(string s)
    {
        var b = new System.Text.StringBuilder();
        foreach (char c in s) if (char.IsLetterOrDigit(c)) b.Append(char.ToLowerInvariant(c));
        return b.ToString();
    }
    static string JsonString(string json, string key)
    {
        var m = System.Text.RegularExpressions.Regex.Match(json, "\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
        return m.Success ? m.Groups[1].Value : null;
    }

    // Mesma regra do runtime (wotm_mods.hpp): pastas com '_' ou '.' no comeco ficam de fora; sem id, o nome da pasta; mesmo
    // monstro e id em dois mods, vale o ultimo em ordem alfabetica.
    void LoadModSkins()
    {
        for (int k = 0; k < modSkins.Length; ++k) modSkins[k] = new List<KeyValuePair<string, string>>();
        string mods = ModsDir();
        if (mods == null) return;
        var dirs = new List<string>(Directory.GetDirectories(mods));
        dirs.Sort(StringComparer.OrdinalIgnoreCase);
        var byKey = new SortedDictionary<string, KeyValuePair<int, KeyValuePair<string, string>>>(StringComparer.Ordinal);
        foreach (string mod in dirs)
        {
            string n = Path.GetFileName(mod);
            if (n.StartsWith("_") || n.StartsWith(".")) continue;
            string skinsDir = Path.Combine(mod, "skins");
            if (!Directory.Exists(skinsDir)) continue;
            foreach (string dir in Directory.GetDirectories(skinsDir))
            {
                string js = Path.Combine(dir, "skin.json");
                if (!File.Exists(js)) continue;
                string text;
                try { text = File.ReadAllText(js); } catch (IOException) { continue; }
                string mon = JsonString(text, "monster");
                if (mon == null) continue;
                int k = -1;
                for (int i = 0; i < MonsterGameNames.Length; ++i)
                    if (Norm(mon) == Norm(MonsterGameNames[i]) || Norm(mon) == Norm(MonsterPrefixes[i])) k = i;
                if (k < 0) continue;
                string id = (JsonString(text, "id") ?? Path.GetFileName(dir)).ToLowerInvariant();
                string name = JsonString(text, "name") ?? id.ToUpperInvariant();
                byKey[k + "/" + id] = new KeyValuePair<int, KeyValuePair<string, string>>(k, new KeyValuePair<string, string>(name, id));
            }
        }
        foreach (var kv in byKey) modSkins[kv.Value.Key].Add(kv.Value.Value);
    }

    // Monstros custom (discroot\mods\<mod>\monsters\<pasta>\monster.json, MODS.md): (nome, id), em ordem de id. So para o jogador: um
    // custom de chefe como IA rodaria a IA do chefe, que precisa da fase dele.
    readonly List<KeyValuePair<string, string>> modCustoms = new List<KeyValuePair<string, string>>();
    void LoadModCustoms()
    {
        modCustoms.Clear();
        string mods = ModsDir();
        if (mods == null) return;
        var dirs = new List<string>(Directory.GetDirectories(mods));
        dirs.Sort(StringComparer.OrdinalIgnoreCase);
        var byId = new SortedDictionary<string, string>(StringComparer.Ordinal);
        foreach (string mod in dirs)
        {
            string n = Path.GetFileName(mod);
            if (n.StartsWith("_") || n.StartsWith(".")) continue;
            string monDir = Path.Combine(mod, "monsters");
            if (!Directory.Exists(monDir)) continue;
            foreach (string dir in Directory.GetDirectories(monDir))
            {
                string js = Path.Combine(dir, "monster.json");
                if (!File.Exists(js)) continue;
                string text;
                try { text = File.ReadAllText(js); } catch (IOException) { continue; }
                if (JsonString(text, "base") == null) continue;
                string id = (JsonString(text, "id") ?? Path.GetFileName(dir)).ToLowerInvariant();
                byId[id] = JsonString(text, "name") ?? id.ToUpperInvariant();
            }
        }
        foreach (var kv in byId) modCustoms.Add(new KeyValuePair<string, string>(kv.Value, kv.Key));
    }
    // Indice do custom na caixa do jogador (depois dos 10 e do Random), ou -1
    int CustomIndex(int sel) { int c = sel - MonsterIds.Length - 1; return c >= 0 && c < modCustoms.Count ? c : -1; }

    static ComboBox SkinBox(int width)
    {
        return new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = width, DropDownWidth = 180 };
    }

    // Itens da caixa de skin para o monstro k (indice em MonsterIds; fora dele = Random: so os 4 do disco)
    void FillSkins(ComboBox cb, int k, string keep)
    {
        cb.BeginUpdate();
        cb.Items.Clear();
        for (int i = 0; i < 4; ++i) cb.Items.Add(new SkinItem("Skin " + (i + 1), i.ToString()));
        if (k >= 0 && k < modSkins.Length && modSkins[k] != null)
            foreach (var s in modSkins[k]) cb.Items.Add(new SkinItem(s.Key, s.Value));
        cb.Items.Add(new SkinItem("Random", RandomSkin));
        cb.EndUpdate();
        cb.SelectedIndex = 0;
        if (keep == RandomId.ToString()) keep = RandomSkin;   // ini de antes das skins de mod
        for (int i = 0; i < cb.Items.Count; ++i)
            if (((SkinItem)cb.Items[i]).Value == keep) cb.SelectedIndex = i;
    }

    static string SkinId(ComboBox cb) { return cb.SelectedItem == null ? "0" : ((SkinItem)cb.SelectedItem).Value; }

    // Monstro escolhido numa caixa (indice em MonsterIds; Random = MonsterIds.Length)
    void WireSkins(ComboBox monster, ComboBox skin, int offset, string saved)
    {
        FillSkins(skin, monster.SelectedIndex - offset, saved);
        monster.SelectedIndexChanged += delegate { FillSkins(skin, monster.SelectedIndex - offset, SkinId(skin)); };
    }
    readonly Label matchInfo = new Label { AutoSize = true, MaximumSize = new Size(400, 0) };

    GroupBox MatchGroup(Dictionary<string, string> saved, ToolTip tips)
    {
        var gb = new DarkGroupBox { Text = "Match", AutoSize = true, Width = 420, Padding = new Padding(8) };
        var inner = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, Dock = DockStyle.Fill, WrapContents = false };
        gb.Controls.Add(inner);
        string v;

        matchOn.Checked = saved.TryGetValue(":MatchOn", out v) && v == "1";
        tips.SetToolTip(matchOn, "Skips the menu and goes straight to the fight (1 player against the CPU, 3 lives, no win target: " +
            "the fight goes on without the end-of-match dialog). With no AI you play alone on the stage. Forces -Auto 0, since the scripted pad would press buttons mid-fight. " +
            "With 'Player controlled by AI' (Run) it becomes AI vs AI.");
        inner.Controls.Add(matchOn);

        matchPlayer.Items.AddRange(MonsterNames);
        matchPlayer.Items.Add("Random");
        LoadModCustoms();
        foreach (var c in modCustoms) matchPlayer.Items.Add(c.Key + " (mod)");
        matchPlayer.SelectedIndex = saved.TryGetValue(":MatchPlayer", out v) && ParseInt(v) == RandomId ? MonsterNames.Length
            : IndexOrZero(MonsterIds, saved, ":MatchPlayer");
        if (v != null)
            for (int i = 0; i < modCustoms.Count; ++i)
                if (modCustoms[i].Value == v) matchPlayer.SelectedIndex = MonsterNames.Length + 1 + i;
        tips.SetToolTip(matchPlayer, "'Random' draws one of the ten monsters when you press Play. After it, the custom monsters of " +
            "discroot\\mods\\<mod>\\monsters\\ (MODS.md), with their own costume.");
        LoadModSkins();
        WireSkins(matchPlayer, matchPlayerSkin, 0, saved.TryGetValue(":MatchPlayerSkin", out v) ? v : "0");
        tips.SetToolTip(matchPlayerSkin, SkinTip);
        var you = Row("Your monster", matchPlayer);
        you.Controls.Add(matchPlayerSkin);
        inner.Controls.Add(you);

        foreach (int id in LevelIds) matchLevel.Items.Add(id + "  " + LevelNames[Array.IndexOf(LevelIds, id)]);
        matchLevel.SelectedIndex = IndexOrZero(LevelIds, saved, ":MatchLevel");
        tips.SetToolTip(matchLevel, "Number = stage index in GLOSSARY.md. Every stage fits 8 monsters: the monsters load above 32 MB (64 MB runtime); " +
            "Metro City with 8 still has ~21 MB to spare.");
        inner.Controls.Add(Row("Stage", matchLevel));

        inner.Controls.Add(new Label { Text = "AI opponents (up to 7; none = play solo on the stage):", AutoSize = true, Margin = new Padding(3, 6, 3, 0) });
        var foes = new FlowLayoutPanel { FlowDirection = FlowDirection.LeftToRight, AutoSize = true, MaximumSize = new Size(420, 0), WrapContents = true };
        for (int i = 0; i < MatchMaxAIs; ++i)
        {
            var cb = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 112 };
            cb.Items.Add("-");
            cb.Items.AddRange(MonsterNames);
            cb.Items.Add("Random");
            int id = saved.TryGetValue(":MatchAI" + (i + 1), out v) ? ParseInt(v) : (i == 0 ? MonsterIds[0] : 0);
            int k = Array.IndexOf(MonsterIds, id);
            cb.SelectedIndex = id == RandomId ? MonsterNames.Length + 1 : (k >= 0 ? k + 1 : 0);
            cb.SelectedIndexChanged += delegate { UpdateMatch(); };
            tips.SetToolTip(cb, "'-' = empty slot. 'Random' draws one of the ten monsters when you press Play.");
            matchAIs[i] = cb;
            var sk = SkinBox(80);
            WireSkins(cb, sk, 1, saved.TryGetValue(":MatchAI" + (i + 1) + "Skin", out v) ? v : "0");
            tips.SetToolTip(sk, SkinTip);
            matchAISkins[i] = sk;
            var pair = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0, 0, 6, 0) };
            pair.Controls.Add(cb);
            pair.Controls.Add(sk);
            foes.Controls.Add(pair);
        }
        inner.Controls.Add(foes);
        matchInfo.ForeColor = DimColor;
        inner.Controls.Add(matchInfo);

        matchOn.CheckedChanged += delegate { UpdateMatch(); };
        matchPlayer.SelectedIndexChanged += delegate { UpdateMatch(); };
        matchLevel.SelectedIndexChanged += delegate { UpdateMatch(); };
        UpdateMatch();
        return gb;
    }

    const string SkinTip = "Costume (the game's 4 per monster; Agamo's Skin 4 is Mecha Sweet Tooth), then the mod skins of that monster " +
        "(discroot\\mods\\<mod>\\skins\\, MODS.md). Locked costumes work too. If the same monster already has a disc skin in the fight, it gets " +
        "the next free one, as in the menu; mod skins can repeat. 'Random' is drawn when you press Play.";

    int MatchFoes()
    {
        int n = 0;
        foreach (ComboBox cb in matchAIs) if (cb.SelectedIndex > 0) ++n;
        return n;
    }

    void UpdateMatch()
    {
        bool on = matchOn.Checked;
        matchPlayer.Enabled = matchLevel.Enabled = on;
        matchPlayerSkin.Enabled = on && CustomIndex(matchPlayer.SelectedIndex) < 0;   // o custom tem o traje dele
        for (int i = 0; i < matchAIs.Length; ++i)
        {
            matchAIs[i].Enabled = on;
            if (matchAISkins[i] != null) matchAISkins[i].Enabled = on && matchAIs[i].SelectedIndex > 0;
        }
        int total = 1 + MatchFoes();
        string txt = total == 1 ? "Solo: just you on the stage." : total + " monsters in the fight.";
        if (total > 4) txt += " Without the enemy bars (Recompiled options, in the main menu), only 4 enemies get an off-screen health marker.";
        matchInfo.Text = txt;
    }

    // "<monstro>:<skin>", com os "Random" sorteados aqui (k = indice em MonsterIds, MonsterIds.Length = Random). Skin aleatoria:
    // entre os 4 do disco e as skins de mod do monstro sorteado.
    string MatchEntry(int k, ComboBox skin)
    {
        int c = CustomIndex(k);
        if (c >= 0) return modCustoms[c].Value;   // o runtime le o id (WOTM_MATCH=goliath,...)
        if (k >= MonsterIds.Length) k = rng.Next(MonsterIds.Length);
        string s = SkinId(skin);
        if (s == RandomSkin)
        {
            int n = rng.Next(4 + modSkins[k].Count);
            s = n < 4 ? n.ToString() : modSkins[k][n - 4].Value;
        }
        return MonsterIds[k] + ":" + s;
    }

    string MatchValue()
    {
        var parts = new List<string> { MatchEntry(matchPlayer.SelectedIndex, matchPlayerSkin) };
        for (int i = 0; i < matchAIs.Length; ++i)
            if (matchAIs[i].SelectedIndex > 0) parts.Add(MatchEntry(matchAIs[i].SelectedIndex - 1, matchAISkins[i]));
        parts.Add(LevelIds[matchLevel.SelectedIndex].ToString());
        return string.Join(",", parts.ToArray());
    }

    static int MonsterId(int k) { return k >= MonsterIds.Length ? RandomId : MonsterIds[k]; }

    // Trapacas do menu F1 ligadas desde o inicio (WOTM_CHEATS=inv,stam|furia,esp,cloak,deus,vencer=N); valem para os jogadores.
    static readonly string[] CheatKeys = { "inv", "esp", "cloak", "deus" };
    static readonly string[] CheatLabels = { "Invincible", "Special always ready", "CLOAK", "God mode (no damage, no hit reaction)" };
    readonly CheckBox[] cheatBoxes = new CheckBox[4];
    readonly ComboBox cheatStamina = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 160 };
    readonly NumericUpDown cheatWin = new NumericUpDown { Minimum = 0, Maximum = 3600, Width = 70 };

    GroupBox CheatsGroup(Dictionary<string, string> saved, ToolTip tips)
    {
        var gb = new DarkGroupBox { Text = "Cheats from the start", AutoSize = true, Width = 420, Padding = new Padding(8) };
        var inner = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, Dock = DockStyle.Fill, WrapContents = false };
        gb.Controls.Add(inner);
        string v;
        inner.Controls.Add(new Label { Text = "Same as the in-fight F1 menu; apply to human players.", AutoSize = true, ForeColor = DimColor });
        for (int i = 0; i < CheatKeys.Length; ++i)
        {
            var cb = new DarkCheckBox { Text = CheatLabels[i], AutoSize = true };
            cb.Checked = saved.TryGetValue(":Cheat_" + CheatKeys[i], out v) && v == "1";
            cheatBoxes[i] = cb;
            inner.Controls.Add(cb);
        }
        tips.SetToolTip(cheatBoxes[0], "Health never drops; hits still push you.");
        tips.SetToolTip(cheatBoxes[3], "The game's m_godMode (full health, stamina and special) plus, from us, no hit reaction. A building falling on you still kills.");
        cheatStamina.Items.AddRange(new object[] { "Normal", "Always full", "Rage (130)" });
        cheatStamina.SelectedIndex = saved.TryGetValue(":CheatStamina", out v) ? Math.Max(0, Math.Min(2, ParseInt(v))) : 0;
        inner.Controls.Add(Row("Stamina", cheatStamina));
        cheatWin.Value = saved.TryGetValue(":CheatWin", out v) ? Math.Max(0, Math.Min(3600, ParseInt(v))) : 0;
        tips.SetToolTip(cheatWin, "Knocks out every living AI every N seconds of game time (F1's 'win the fight'). 0 = off.");
        inner.Controls.Add(Row("Win every (s, 0 = off)", cheatWin));
        return gb;
    }

    string CheatsValue()
    {
        var parts = new List<string>();
        for (int i = 0; i < CheatKeys.Length; ++i) if (cheatBoxes[i].Checked) parts.Add(CheatKeys[i]);
        if (cheatStamina.SelectedIndex == 1) parts.Add("stam");
        else if (cheatStamina.SelectedIndex == 2) parts.Add("furia");
        if (cheatWin.Value > 0) parts.Add("vencer=" + (int)cheatWin.Value);
        return string.Join(",", parts.ToArray());
    }

    static int ParseInt(string s)
    {
        int n;
        return int.TryParse(s, out n) ? n : 0;
    }

    static int IndexOrZero(int[] ids, Dictionary<string, string> saved, string key)
    {
        string v;
        int k = saved.TryGetValue(key, out v) ? Array.IndexOf(ids, ParseInt(v)) : -1;
        return k >= 0 ? k : 0;
    }

    static FlowLayoutPanel Row(string label, Control c)
    {
        var p = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0) };
        p.Controls.Add(new Label { Text = label, AutoSize = false, Size = new Size(210, 23), TextAlign = ContentAlignment.MiddleLeft });
        p.Controls.Add(c);
        return p;
    }

    Dictionary<string, string> LoadIni()
    {
        var d = new Dictionary<string, string>();
        try
        {
            foreach (string line in File.ReadAllLines(IniPath))
            {
                int i = line.LastIndexOf('|');
                if (i > 0) d[line.Substring(0, i)] = line.Substring(i + 1).Trim();
            }
        }
        catch (IOException) { }
        return d;
    }

    void SaveIni()
    {
        var lines = new List<string>();
        foreach (var kv in boxes) lines.Add(kv.Key.Key + "|" + (kv.Value.Checked ? "1" : "0"));
        lines.Add(":MatchOn|" + (matchOn.Checked ? "1" : "0"));
        int cu = CustomIndex(matchPlayer.SelectedIndex);
        lines.Add(":MatchPlayer|" + (cu >= 0 ? modCustoms[cu].Value : MonsterId(matchPlayer.SelectedIndex).ToString()));
        lines.Add(":MatchPlayerSkin|" + SkinId(matchPlayerSkin));
        lines.Add(":MatchLevel|" + LevelIds[matchLevel.SelectedIndex]);
        for (int i = 0; i < matchAIs.Length; ++i)
        {
            lines.Add(":MatchAI" + (i + 1) + "|" + (matchAIs[i].SelectedIndex > 0 ? MonsterId(matchAIs[i].SelectedIndex - 1) : 0));
            lines.Add(":MatchAI" + (i + 1) + "Skin|" + SkinId(matchAISkins[i]));
        }
        for (int i = 0; i < CheatKeys.Length; ++i) lines.Add(":Cheat_" + CheatKeys[i] + "|" + (cheatBoxes[i].Checked ? "1" : "0"));
        lines.Add(":CheatStamina|" + cheatStamina.SelectedIndex);
        lines.Add(":CheatWin|" + (int)cheatWin.Value);
        try { File.WriteAllLines(IniPath, lines.ToArray()); } catch (IOException) { } catch (UnauthorizedAccessException) { }
    }

    // Video, QoL e teclado separado ficam no painel "Recompiled options" do menu principal (native/wotm_settings.hpp, settings.ini
    // ao lado do save.json). O launcher nao os passa; uma variavel herdada passaria por cima do arquivo, entao sai do ambiente.
    static readonly string[] SettingsKeys = { "WOTM_RES", "WOTM_ASPECT", "WOTM_FULLSCREEN", "WOTM_HZ", "WOTM_VSYNC",
        "WOTM_LOCKTOGGLE", "WOTM_ENEMYBARS", "WOTM_DAMAGEPOP", "WOTM_KBSEPARATE", "WOTM_SKIPINTRO", "WOTM_VIBRATION", "WOTM_INTERP" };

    // A pasta do runtime (a que tem run_live.ps1): WOTM_RECOMP_DIR, depois o .wotm_recomp_dir que o run_live.bat salva na raiz do repo.
    string KnownRuntimeDir()
    {
        string env = Environment.GetEnvironmentVariable("WOTM_RECOMP_DIR");
        if (!string.IsNullOrEmpty(env) && File.Exists(Path.Combine(env, "run_live.ps1"))) return env;
        string saved = Path.Combine(exeDir, ".wotm_recomp_dir");
        if (File.Exists(saved))
        {
            string dir = File.ReadAllText(saved).Trim();
            if (File.Exists(Path.Combine(dir, "run_live.ps1"))) return dir;
        }
        return null;
    }
    // A pasta mods que o jogo le (wotm_mods.inc): WOTM_MODS; senao mods\ ao lado do ELF (WOTM_DISC ou disc\ do repo, como o
    // run_live.ps1); sem ela, mods\ do repo. null sem nenhuma.
    string ModsDir()
    {
        string env = Environment.GetEnvironmentVariable("WOTM_MODS");
        if (env == "0") return null;
        if (!string.IsNullOrEmpty(env)) return Directory.Exists(env) ? env : null;
        var dirs = new List<string>();
        string disc = Environment.GetEnvironmentVariable("WOTM_DISC");
        if (!string.IsNullOrEmpty(disc)) dirs.Add(Path.Combine(disc, "mods"));
        dirs.Add(Path.Combine(Path.Combine(exeDir, "disc"), "mods"));
        dirs.Add(Path.Combine(exeDir, "mods"));
        foreach (string d in dirs)
        {
            string parent = Path.GetDirectoryName(d);
            if (!Directory.Exists(parent)) continue;
            foreach (string sub in Directory.GetDirectories(parent))
                if (Path.GetFileName(sub) == "mods") return sub;   // nome exato: "mods" casaria com o MOD\ do disco
        }
        return null;
    }
    // Como o KnownRuntimeDir; sem pasta conhecida, pergunta
    string RuntimeDir()
    {
        string known = KnownRuntimeDir();
        if (known != null) return known;
        string saved = Path.Combine(exeDir, ".wotm_recomp_dir");
        using (var dlg = new FolderBrowserDialog { Description = "Runtime folder (the one containing run_live.ps1)" })
        {
            if (dlg.ShowDialog(this) == DialogResult.OK && File.Exists(Path.Combine(dlg.SelectedPath, "run_live.ps1")))
            {
                File.WriteAllText(saved, dlg.SelectedPath + Environment.NewLine);
                return dlg.SelectedPath;
            }
        }
        return null;
    }

    bool On(string key)
    {
        foreach (var kv in boxes) if (kv.Key.Key == key) return kv.Value.Checked;
        return false;
    }

    void Play()
    {
        SaveIni();
        string rt = RuntimeDir();
        if (rt == null) { status.Text = "Runtime folder not found (run_live.ps1)."; return; }
        // partida direta pula o menu: o roteiro do pad (-Auto 1) apertaria botoes no meio da luta
        string args = string.Format("-NoProfile -ExecutionPolicy Bypass -File \"{0}\" -Auto {1} -Stats {2} -Build {3}",
            Path.Combine(rt, "run_live.ps1"), On(":Auto") && !matchOn.Checked ? 1 : 0, On(":Stats") ? 1 : 0, "build_dev");
        var psi = new ProcessStartInfo("powershell.exe", args) { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = rt };
        psi.EnvironmentVariables["WOTM_REPO"] = exeDir;
        foreach (string k in SettingsKeys) psi.EnvironmentVariables.Remove(k);
        foreach (var kv in boxes)
        {
            string key = kv.Key.Key;
            if (key.StartsWith(":")) continue;
            string name = key, value = "1";
            int eq = key.IndexOf('=');
            if (eq > 0) { name = key.Substring(0, eq); value = key.Substring(eq + 1); }
            if (kv.Value.Checked) psi.EnvironmentVariables[name] = value;
            else psi.EnvironmentVariables.Remove(name);
        }
        if (matchOn.Checked) psi.EnvironmentVariables["WOTM_MATCH"] = MatchValue();
        else psi.EnvironmentVariables.Remove("WOTM_MATCH");
        string cheats = CheatsValue();
        if (cheats.Length > 0) psi.EnvironmentVariables["WOTM_CHEATS"] = cheats;
        else psi.EnvironmentVariables.Remove("WOTM_CHEATS");
        try
        {
            Process.Start(psi);
            Close();   // o jogo segue sozinho (processo independente); em erro a janela fica com a mensagem
        }
        catch (Exception e) { status.Text = "Failed: " + e.Message; }
    }

    // Kit de mods (tools\modkit_gui.py, docs\guides\MODS.md): pythonw sem console; sem ele, python
    void OpenModKit()
    {
        string script = Path.Combine(exeDir, "tools", "modkit_gui.py");
        if (!File.Exists(script)) { status.Text = "tools\\modkit_gui.py not found next to the launcher."; return; }
        foreach (string py in new[] { "pythonw.exe", "pyw.exe", "python.exe", "py.exe" })
        {
            try
            {
                Process.Start(new ProcessStartInfo(py, "\"" + script + "\"") { WorkingDirectory = exeDir, UseShellExecute = false });
                status.Text = "Mod kit opened.";
                return;
            }
            catch (Exception) { }
        }
        status.Text = "Python not found: install Python 3 (with Pillow: pip install pillow) to use the mod kit.";
    }

    void OpenLog()
    {
        string rt = RuntimeDir();
        string p = rt == null ? null : Path.Combine(rt, "logs", "live.err.log");
        if (p != null && !File.Exists(p)) p = Path.Combine(rt, "live.err.log");   // run_live.ps1 antigo grava na raiz
        if (p != null && File.Exists(p)) Process.Start("notepad.exe", "\"" + p + "\"");
        else status.Text = "No live.err.log yet.";
    }

    // Tema: segue o modo dos aplicativos do Windows (Personalizacao > Cores). Escuro = cores abaixo e barra de titulo escura (DWM).
    internal static readonly bool Dark = AppsUseDark();
    static readonly Color BgColor = Color.FromArgb(32, 32, 32), InputColor = Color.FromArgb(51, 51, 51), ButtonColor = Color.FromArgb(58, 58, 58);
    static readonly Color FgColor = Color.FromArgb(232, 232, 232);
    static readonly Color DimColor = Dark ? Color.FromArgb(150, 150, 150) : SystemColors.GrayText;

    static bool AppsUseDark()
    {
        try
        {
            object v = Microsoft.Win32.Registry.GetValue(@"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize", "AppsUseLightTheme", 1);
            return v is int && (int)v == 0;
        }
        catch (Exception) { return false; }
    }

    [DllImport("dwmapi.dll")]
    static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);
    [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)]
    static extern int SetWindowTheme(IntPtr hwnd, string appName, string idList);
    // SetPreferredAppMode (sem nome exportado, ordinal 135, Windows 10 1903+): 1 = segue o sistema; sem ele a lista aberta sai clara
    [DllImport("uxtheme.dll", EntryPoint = "#135")]
    static extern int SetPreferredAppMode(int mode);

    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        if (!Dark) return;
        int on = 1;
        try
        {   // DWMWA_USE_IMMERSIVE_DARK_MODE: 20 no Windows 10 20H1+ e 11, 19 antes
            if (DwmSetWindowAttribute(Handle, 20, ref on, 4) != 0) DwmSetWindowAttribute(Handle, 19, ref on, 4);
        }
        catch (Exception) { }
    }

    // Texto com a cor padrao vira claro; cores escolhidas (cinza das notas, aviso vermelho) ficam.
    static void ApplyDark(Control c)
    {
        if (c.ForeColor == SystemColors.ControlText) c.ForeColor = FgColor;
        if (c is ComboBox)
        {   // tema escuro nativo do Windows (o das caixas de arquivo): legivel tambem desabilitado, ao contrario do FlatStyle com cores
            c.BackColor = InputColor;
            c.ForeColor = FgColor;   // o padrao da ComboBox e WindowText (nao ControlText): os itens da lista aberta saiam pretos
            Control cc = c;
            EventHandler theme = delegate { try { SetWindowTheme(cc.Handle, "DarkMode_CFD", null); } catch (Exception) { } };
            if (c.IsHandleCreated) theme(c, EventArgs.Empty);
            else c.HandleCreated += theme;
        }
        else if (c is NumericUpDown)
        {   // a cor tem que ir para a caixa de texto interna (UpDownEdit) tambem
            c.BackColor = InputColor;
            ((NumericUpDown)c).BorderStyle = BorderStyle.FixedSingle;
            foreach (Control k in c.Controls) if (k is TextBox) { k.BackColor = InputColor; k.ForeColor = FgColor; }
        }
        else if (c is Button)
        {
            var b = (Button)c;
            b.FlatStyle = FlatStyle.Flat;
            b.BackColor = ButtonColor;
            b.FlatAppearance.BorderColor = Color.FromArgb(100, 100, 100);
        }
        else c.BackColor = BgColor;
        if (!(c is NumericUpDown)) foreach (Control k in c.Controls) ApplyDark(k);
    }

    [STAThread]
    static void Main()
    {
        Application.EnableVisualStyles();
        if (Dark) try { SetPreferredAppMode(1); } catch (Exception) { }
        Application.Run(new Launcher());
    }
}
