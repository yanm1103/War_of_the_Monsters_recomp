// Launcher do jogo ao vivo: janela com caixas de marcar para as flags de ambiente (WOTM_*, PS2X_*) e um botao que roda o
// run_live.ps1 do runtime com elas. Para adicionar uma flag, acrescente uma linha em Flags. O estado fica em WotM_Launcher.ini,
// ao lado do executavel. Compilar: tools\launcher\build.bat (csc do .NET Framework, C# 5).
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
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

class Launcher : Form
{
    // Chaves com ':' sao parametros do run_live.ps1; as outras viram variaveis de ambiente (=1 quando marcadas, removidas quando nao).
    // Diagnosticos sem caixa continuam valendo pela variavel (WOTM_RATEMIN=0, WOTM_VU0_INTERP, PS2X_PAD_DEADZONE; tabela no NATIVO.md).
    static readonly Flag[] Flags = {
        new Flag("Mods", "WOTM_MODCAM", false, "Camera moderna (so o analogico direito gira a camera)",
            "Na luta, a camera gira em volta do seu monstro so pelo analogico direito; o monstro anda e vira sem levar a camera junto. " +
            "Corpo a corpo, nocaute, vitoria e cinemas seguem com a camera original. Desmarcado: camera original do jogo."),

        new Flag("Execucao", ":Auto", true, "Roteiro automatico ate a fase", "-Auto 1: o pad do roteiro joga ate uma fase (~50 s). Desmarcado: voce joga com o teclado."),
        new Flag("Execucao", "WOTM_FOTOS", true, "Fotos para depurar (F7 pausa, F6 avanca, F8 foto com nota)", "F8 grava o quadro (instantaneo da checagem VU1 x nativo, ~40 MB), um PNG da janela e a sua nota em <runtime>\\fotos\\<data>. Apague as fotos depois de resolvidas."),
        new Flag("Execucao", "WOTM_AIVSAI", false, "Jogador controlado pela IA do jogo", "O monstro do jogador 1 vira IA (playerInit -> aiInit): luta IA contra IA. Para lotes sem menu use tools\\lote.ps1."),
        new Flag("Execucao", "WOTM_WATCHDOG=0", false, "Desligar o vigia de congelamento", "Por padrao, g_frame parado por 8 s (fora de filme) grava [vigia] no live.err.log e ram_/vram_vigia_<g_frame>.bin no discroot; quadros do jogo abaixo de 25/s viram aviso [vigia] LENTO."),
        new Flag("Execucao", "WOTM_FMV_MUTE", false, "Filmes sem audio", ""),

        new Flag("Diagnostico de desenho", "WOTM_NOFX", false, "Sem particulas/efeitos", "Desliga as particulas de fx[] e os efeitos de wotm_fx."),
        new Flag("Diagnostico de desenho", "WOTM_NOSHADOW", false, "Sem sombras", "Desliga as sombras dos bloqueadores (monstros e afins)."),
        new Flag("Diagnostico de desenho", "WOTM_NOSKIN", false, "Monstros na pose de referencia (sem skinning)", "Diagnostico de malha."),
        new Flag("Diagnostico de desenho", "WOTM_NOVCOL", false, "Sem cor de vertice", "Ignora o sombreamento por vertice."),
        new Flag("Diagnostico de desenho", "WOTM_OPAQUE", false, "Ignorar o alfa de vertice nos skinned", "Diagnostico de transparencia."),
        new Flag("Diagnostico de desenho", "WOTM_ONEPASS", false, "Tudo num passe so", "Nao separa os translucidos num segundo passe."),

        new Flag("Logs (live.err.log)", ":Stats", false, "Estatisticas (PS2X_STATS)", "-Stats 1: linhas [native]/[ui]/[time]."),
        new Flag("Logs (live.err.log)", "WOTM_FIELDLOG", false, "Fields por quadro", "Uma linha [field] por quadro do jogo: fields, updateRate, ciclos do EE, tempo de host e os lacos que mais gastam."),
        new Flag("Logs (live.err.log)", "WOTM_TURNLOG", false, "Rotacao dos monstros", "Uma linha [turn] por monstro por quadro do jogo: rate, aceleracao e velocidade de giro, angulos."),
        new Flag("Logs (live.err.log)", "WOTM_CAMLOG", false, "Camera a cada desenho", "Uma linha [cam] por desenho: t, g_frame, fov, posicao e direcao."),
        new Flag("Logs (live.err.log)", "PS2X_PADLOG", false, "Gamepad (nome e eixos)", "Linhas [pad]: nome do gamepad 0 e cada mudanca dos analogicos, com o horario."),
        new Flag("Logs (live.err.log)", "PS2X_SND_LOG", false, "Som nativo", "Chamadas do 989snd."),
    };

    // Video: altura do quadro nativo (WOTM_RES) e proporcao (WOTM_ASPECT; "largura dinamica" = sem ela, segue a janela/monitor)
    static readonly string[] Resolutions = { "448", "720", "1080", "1440", "2160" };
    static readonly string[] ResolutionLabels = { "448 (original do PS2)", "720", "1080", "1440", "2160" };
    static readonly string[] Aspects = { "4:3", "16:9", "16:10", "21:9", "32:9" };
    const string DefaultRes = "720", DefaultAspect = "16:9";

    readonly Dictionary<Flag, CheckBox> boxes = new Dictionary<Flag, CheckBox>();
    readonly ComboBox res = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 160 };
    readonly ComboBox aspect = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 80 };
    readonly CheckBox dynWidth = new CheckBox { Text = "Largura dinamica (proporcao segue a janela)", AutoSize = true };
    readonly CheckBox fullscreen = new CheckBox { Text = "Tela cheia   [WOTM_FULLSCREEN]", AutoSize = true };
    readonly string exeDir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
    string IniPath { get { return Path.Combine(exeDir, "WotM_Launcher.ini"); } }
    readonly Label status = new Label();

    Launcher()
    {
        Text = "WotM - opcoes";
        Font = new Font("Segoe UI", 9f);
        AutoSize = true;
        AutoSizeMode = AutoSizeMode.GrowAndShrink;
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;

        var tips = new ToolTip();
        var root = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, Padding = new Padding(10), WrapContents = false };
        Dictionary<string, string> saved = LoadIni();
        root.Controls.Add(VideoGroup(saved, tips));
        GroupBox gb = null;
        FlowLayoutPanel inner = null;
        foreach (Flag f in Flags)
        {
            if (gb == null || gb.Text != f.Group)
            {
                gb = new GroupBox { Text = f.Group, AutoSize = true, Width = 420, Padding = new Padding(8) };
                inner = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, Dock = DockStyle.Fill, WrapContents = false };
                gb.Controls.Add(inner);
                root.Controls.Add(gb);
            }
            var cb = new CheckBox { Text = f.Label + "   [" + f.Key.TrimStart(':') + "]", AutoSize = true };
            string v;
            cb.Checked = saved.TryGetValue(f.Key, out v) ? v == "1" : f.Default;
            if (f.Tip.Length > 0) tips.SetToolTip(cb, f.Tip);
            inner.Controls.Add(cb);
            boxes[f] = cb;
        }

        var buttons = new FlowLayoutPanel { AutoSize = true };
        var play = new Button { Text = "Jogar", AutoSize = true };
        play.Click += delegate { Play(); };
        var log = new Button { Text = "Abrir live.err.log", AutoSize = true };
        log.Click += delegate { OpenLog(); };
        var reset = new Button { Text = "Padrao", AutoSize = true };
        reset.Click += delegate
        {
            foreach (var kv in boxes) kv.Value.Checked = kv.Key.Default;
            res.SelectedIndex = Array.IndexOf(Resolutions, DefaultRes);
            aspect.SelectedIndex = Array.IndexOf(Aspects, DefaultAspect);
            dynWidth.Checked = true;
            fullscreen.Checked = false;
        };
        buttons.Controls.Add(play);
        buttons.Controls.Add(log);
        buttons.Controls.Add(reset);
        root.Controls.Add(buttons);
        status.AutoSize = true;
        status.MaximumSize = new Size(420, 0);
        root.Controls.Add(status);
        Controls.Add(root);
        AcceptButton = play;
        FormClosing += delegate { SaveIni(); };
    }

    GroupBox VideoGroup(Dictionary<string, string> saved, ToolTip tips)
    {
        var gb = new GroupBox { Text = "Video", AutoSize = true, Width = 420, Padding = new Padding(8) };
        var inner = new FlowLayoutPanel { FlowDirection = FlowDirection.TopDown, AutoSize = true, Dock = DockStyle.Fill, WrapContents = false };
        gb.Controls.Add(inner);
        string v;

        res.Items.AddRange(ResolutionLabels);
        int ri = Array.IndexOf(Resolutions, saved.TryGetValue("WOTM_RES", out v) ? v : DefaultRes);
        res.SelectedIndex = ri >= 0 ? ri : Array.IndexOf(Resolutions, DefaultRes);
        tips.SetToolTip(res, "Altura do quadro nativo em pixels (WOTM_RES); a largura sai da proporcao. Independe do tamanho da janela: " +
            "a janela so amplia ou reduz o quadro.");
        inner.Controls.Add(Row("Resolucao (linhas)   [WOTM_RES]", res));

        dynWidth.Checked = !saved.TryGetValue(":DynWidth", out v) || v == "1";
        tips.SetToolTip(dynWidth, "Marcado: a largura acompanha a janela (ou o monitor, em tela cheia), de 4:3 a 32:9. " +
            "Desmarcado: proporcao fixa (WOTM_ASPECT), com faixas pretas se a janela nao bater.");
        inner.Controls.Add(dynWidth);

        aspect.Items.AddRange(Aspects);
        int ai = Array.IndexOf(Aspects, saved.TryGetValue("WOTM_ASPECT", out v) ? v : DefaultAspect);
        aspect.SelectedIndex = ai >= 0 ? ai : Array.IndexOf(Aspects, DefaultAspect);
        tips.SetToolTip(aspect, "4:3 = tela original do jogo.");
        inner.Controls.Add(Row("Proporcao fixa   [WOTM_ASPECT]", aspect));
        aspect.Enabled = !dynWidth.Checked;
        dynWidth.CheckedChanged += delegate { aspect.Enabled = !dynWidth.Checked; };

        fullscreen.Checked = saved.TryGetValue("WOTM_FULLSCREEN", out v) && v == "1";
        tips.SetToolTip(fullscreen, "Tela cheia sem borda no monitor da janela. Sem ela a janela abre no tamanho da resolucao (reduzida se nao couber).");
        inner.Controls.Add(fullscreen);
        return gb;
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
        lines.Add("WOTM_RES|" + Resolutions[res.SelectedIndex]);
        lines.Add("WOTM_ASPECT|" + Aspects[aspect.SelectedIndex]);
        lines.Add(":DynWidth|" + (dynWidth.Checked ? "1" : "0"));
        lines.Add("WOTM_FULLSCREEN|" + (fullscreen.Checked ? "1" : "0"));
        try { File.WriteAllLines(IniPath, lines.ToArray()); } catch (IOException) { } catch (UnauthorizedAccessException) { }
    }

    // A pasta do runtime (a que tem run_live.ps1): WOTM_RECOMP_DIR, depois o .wotm_recomp_dir que o run_live.bat salva na raiz do repo.
    string RuntimeDir()
    {
        string env = Environment.GetEnvironmentVariable("WOTM_RECOMP_DIR");
        if (!string.IsNullOrEmpty(env) && File.Exists(Path.Combine(env, "run_live.ps1"))) return env;
        string saved = Path.Combine(exeDir, ".wotm_recomp_dir");
        if (File.Exists(saved))
        {
            string dir = File.ReadAllText(saved).Trim();
            if (File.Exists(Path.Combine(dir, "run_live.ps1"))) return dir;
        }
        using (var dlg = new FolderBrowserDialog { Description = "Pasta do runtime (a que contem run_live.ps1)" })
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
        if (rt == null) { status.Text = "Nao achei a pasta do runtime (run_live.ps1)."; return; }
        string args = string.Format("-NoProfile -ExecutionPolicy Bypass -File \"{0}\" -Auto {1} -Stats {2} -Build {3}",
            Path.Combine(rt, "run_live.ps1"), On(":Auto") ? 1 : 0, On(":Stats") ? 1 : 0, "build_dev");
        var psi = new ProcessStartInfo("powershell.exe", args) { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = rt };
        psi.EnvironmentVariables["WOTM_REPO"] = exeDir;
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
        psi.EnvironmentVariables["WOTM_RES"] = Resolutions[res.SelectedIndex];
        if (dynWidth.Checked) psi.EnvironmentVariables.Remove("WOTM_ASPECT");
        else psi.EnvironmentVariables["WOTM_ASPECT"] = Aspects[aspect.SelectedIndex];
        if (fullscreen.Checked) psi.EnvironmentVariables["WOTM_FULLSCREEN"] = "1";
        else psi.EnvironmentVariables.Remove("WOTM_FULLSCREEN");
        try
        {
            Process.Start(psi);
            status.Text = "Iniciado (" + rt + "). Saida em live.out.log / live.err.log.";
        }
        catch (Exception e) { status.Text = "Falhou: " + e.Message; }
    }

    void OpenLog()
    {
        string rt = RuntimeDir();
        string p = rt == null ? null : Path.Combine(rt, "live.err.log");
        if (p != null && File.Exists(p)) Process.Start("notepad.exe", "\"" + p + "\"");
        else status.Text = "Ainda nao ha live.err.log.";
    }

    [STAThread]
    static void Main()
    {
        Application.EnableVisualStyles();
        Application.Run(new Launcher());
    }
}
