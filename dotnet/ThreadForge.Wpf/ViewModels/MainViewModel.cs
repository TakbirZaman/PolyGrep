using System.Collections.Concurrent;
using System.Collections.ObjectModel;
using System.Windows.Threading;
using Microsoft.Win32;
using ThreadForge.Interop;

namespace ThreadForge.Wpf.ViewModels;

public sealed class MainViewModel : ObservableObject, IDisposable
{
    private readonly SearchService _service = new();
    private readonly ConcurrentQueue<MatchItem> _pending = new();  // producer: native worker threads, consumer: UI timer
    private readonly DispatcherTimer _timer;

    private string _root = Environment.CurrentDirectory;
    private string _pattern = "";
    private string _extensions = "cpp,h,hpp,cs,py,js,ts,txt,md";
    private bool _useRegex, _caseSensitive, _isRunning;
    private uint _threads;
    private ulong _maxResults = 50_000;
    private string _status = "Ready";

    public MainViewModel()
    {
        SearchCommand = new RelayCommand(SearchAsync, () => !IsRunning && Pattern.Length > 0);
        CancelCommand = new RelayCommand(() => _service.Cancel(), () => IsRunning);
        BrowseCommand = new RelayCommand(Browse, () => !IsRunning);
        _timer = new DispatcherTimer(TimeSpan.FromMilliseconds(120), DispatcherPriority.Background, (_, _) => Pump(), Dispatcher.CurrentDispatcher);
    }

    public ObservableCollection<MatchItem> Matches { get; } = [];

    public RelayCommand SearchCommand { get; }
    public RelayCommand CancelCommand { get; }
    public RelayCommand BrowseCommand { get; }

    public string Root { get => _root; set => Set(ref _root, value); }
    public string Pattern { get => _pattern; set => Set(ref _pattern, value); }
    public string Extensions { get => _extensions; set => Set(ref _extensions, value); }
    public bool UseRegex { get => _useRegex; set => Set(ref _useRegex, value); }
    public bool CaseSensitive { get => _caseSensitive; set => Set(ref _caseSensitive, value); }
    public uint Threads { get => _threads; set => Set(ref _threads, value); }
    public ulong MaxResults { get => _maxResults; set => Set(ref _maxResults, value); }
    public string Status { get => _status; private set => Set(ref _status, value); }
    public bool IsRunning { get => _isRunning; private set { if (Set(ref _isRunning, value)) { Raise(nameof(IsIdle)); System.Windows.Input.CommandManager.InvalidateRequerySuggested(); } } }
    public bool IsIdle => !IsRunning;

    private async Task SearchAsync()
    {
        Matches.Clear();
        _pending.Clear();
        IsRunning = true;
        _timer.Start();
        try
        {
            await _service.RunAsync(new SearchRequest(Root, Pattern, Extensions, UseRegex, CaseSensitive, Threads, MaxResults), _pending);
        }
        catch (SearchException ex)
        {
            Status = "Error: " + ex.Message;
            throw;                     // RelayCommand shows the message box
        }
        finally
        {
            _timer.Stop();
            Pump();                    // flush the tail
            IsRunning = false;
        }
    }

    /// <summary>UI-thread drain: bounded batch per tick keeps the window responsive under heavy result rates.</summary>
    private void Pump()
    {
        for (int i = 0; i < 5000 && _pending.TryDequeue(out var m); i++) Matches.Add(m);
        var s = _service.GetStats();
        Status = $"{s.Matches:N0} matches  |  {s.FilesScanned:N0} files ({s.FilesSkipped:N0} skipped)  |  {s.BytesRead / 1048576.0:F1} MiB  |  {s.ElapsedMs:N0} ms{(s.Running ? "  |  scanning..." : "")}";
    }

    private void Browse()
    {
        var dlg = new OpenFolderDialog { InitialDirectory = Root };
        if (dlg.ShowDialog() == true) Root = dlg.FolderName;
    }

    public void Dispose()
    {
        _timer.Stop();
        _service.Dispose();
    }
}
