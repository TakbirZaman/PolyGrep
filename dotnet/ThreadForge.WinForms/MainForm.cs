using System.Collections.Concurrent;
using System.Diagnostics;
using ThreadForge.Interop;

namespace ThreadForge.WinForms;

/// <summary>Hand-written (designer-free) WinForms UI. Uses a virtual-mode ListView so 100k+ rows stay fast.</summary>
public sealed class MainForm : Form
{
    private readonly SearchService _service = new();
    private readonly ConcurrentQueue<MatchItem> _pending = new();
    private readonly List<MatchItem> _rows = [];
    private readonly System.Windows.Forms.Timer _timer = new() { Interval = 120 };

    private readonly TextBox _root = new() { Text = Environment.CurrentDirectory };
    private readonly TextBox _pattern = new() { PlaceholderText = "text or regex to find" };
    private readonly TextBox _ext = new() { Text = "cpp,h,hpp,cs,py,js,ts,txt,md" };
    private readonly CheckBox _regex = new() { Text = "Regex", AutoSize = true };
    private readonly CheckBox _case = new() { Text = "Case sensitive", AutoSize = true };
    private readonly NumericUpDown _threads = new() { Minimum = 0, Maximum = 256, Width = 55 };
    private readonly NumericUpDown _max = new() { Minimum = 0, Maximum = 1_000_000, Value = 50_000, Width = 80 };
    private readonly Button _search = new() { Text = "Search", AutoSize = true };
    private readonly Button _cancel = new() { Text = "Cancel", AutoSize = true, Enabled = false };
    private readonly Button _browse = new() { Text = "Browse...", AutoSize = true };
    private readonly ListView _list = new() { View = View.Details, VirtualMode = true, FullRowSelect = true, GridLines = false, Dock = DockStyle.Fill };
    private readonly ToolStripStatusLabel _status = new("Ready");

    public MainForm()
    {
        Text = "PolyGrep - WinForms";
        Size = new Size(980, 640);
        MinimumSize = new Size(720, 420);

        _list.Columns.Add("File", 340);
        _list.Columns.Add("Line", 60, HorizontalAlignment.Right);
        _list.Columns.Add("Text", 520);

        var grid = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 6, Padding = new Padding(6) };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 60));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 40));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        foreach (var c in new Control[] { _root, _pattern, _ext }) c.Dock = DockStyle.Fill;

        grid.Controls.Add(Lbl("Folder:"), 0, 0);
        grid.Controls.Add(_root, 1, 0); grid.SetColumnSpan(_root, 4);
        grid.Controls.Add(_browse, 5, 0);
        grid.Controls.Add(Lbl("Pattern:"), 0, 1);
        grid.Controls.Add(_pattern, 1, 1);
        grid.Controls.Add(Lbl("Ext:"), 2, 1);
        grid.Controls.Add(_ext, 3, 1); grid.SetColumnSpan(_ext, 2);
        grid.Controls.Add(_search, 5, 1);

        var opts = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, WrapContents = false };
        opts.Controls.AddRange([_regex, _case, Lbl("Threads (0 = auto):"), _threads, Lbl("Max results (0 = all):"), _max]);
        grid.Controls.Add(opts, 1, 2); grid.SetColumnSpan(opts, 4);
        grid.Controls.Add(_cancel, 5, 2);

        var strip = new StatusStrip();
        strip.Items.Add(_status);

        Controls.Add(_list);   // Fill first, then Top/Bottom docks
        Controls.Add(grid);
        Controls.Add(strip);
        AcceptButton = _search;

        _list.RetrieveVirtualItem += (_, e) =>
        {
            var m = _rows[e.ItemIndex];
            e.Item = new ListViewItem([m.File, m.Line.ToString(), m.Text.Trim()]);
        };
        _list.DoubleClick += (_, _) =>
        {
            if (_list.SelectedIndices.Count == 1)
                Process.Start(new ProcessStartInfo(_rows[_list.SelectedIndices[0]].File) { UseShellExecute = true });
        };
        _browse.Click += (_, _) =>
        {
            using var dlg = new FolderBrowserDialog { SelectedPath = _root.Text };
            if (dlg.ShowDialog(this) == DialogResult.OK) _root.Text = dlg.SelectedPath;
        };
        _search.Click += async (_, _) => await SearchAsync();
        _cancel.Click += (_, _) => _service.Cancel();
        _timer.Tick += (_, _) => Pump();
        FormClosed += (_, _) => { _timer.Dispose(); _service.Dispose(); };
    }

    private static Label Lbl(string t) => new() { Text = t, AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(3, 6, 3, 0) };

    private async Task SearchAsync()
    {
        if (_pattern.Text.Length == 0) { _status.Text = "Enter a pattern first"; return; }

        _rows.Clear();
        _list.VirtualListSize = 0;
        _pending.Clear();
        SetRunning(true);
        _timer.Start();
        try
        {
            var req = new SearchRequest(_root.Text, _pattern.Text, _ext.Text, _regex.Checked, _case.Checked, (uint)_threads.Value, (ulong)_max.Value);
            await _service.RunAsync(req, _pending);
        }
        catch (SearchException ex)
        {
            MessageBox.Show(this, ex.Message, "PolyGrep", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
        finally
        {
            _timer.Stop();
            Pump();
            SetRunning(false);
        }
    }

    private void Pump()
    {
        int added = 0;
        while (added < 5000 && _pending.TryDequeue(out var m)) { _rows.Add(m); added++; }
        if (added > 0) _list.VirtualListSize = _rows.Count;

        var s = _service.GetStats();
        _status.Text = $"{s.Matches:N0} matches  |  {s.FilesScanned:N0} files ({s.FilesSkipped:N0} skipped)  |  {s.BytesRead / 1048576.0:F1} MiB  |  {s.ElapsedMs:N0} ms{(s.Running ? "  |  scanning..." : "")}";
    }

    private void SetRunning(bool running)
    {
        _search.Enabled = !running;
        _browse.Enabled = !running;
        _cancel.Enabled = running;
        _root.Enabled = _pattern.Enabled = _ext.Enabled = !running;
    }
}
