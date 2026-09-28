using System.Collections.Concurrent;
using System.Runtime.InteropServices;

namespace ThreadForge.Interop;

public sealed record MatchItem(string File, ulong Line, string Text);

public sealed record SearchRequest(
    string Root,
    string Pattern,
    string Extensions = "",
    bool UseRegex = false,
    bool CaseSensitive = false,
    uint Threads = 0,
    ulong MaxMatches = 0);

public readonly record struct SearchStats(
    ulong FilesScanned, ulong FilesSkipped, ulong BytesRead, ulong Matches, ulong ElapsedMs, bool Running);

public sealed class SearchException(string message) : Exception(message);

/// <summary>
/// Managed wrapper over the native engine. The native side scans on its own C++ thread pool;
/// this class runs the blocking call on a .NET worker (Task.Run) and hands matches to the UI
/// through a lock-free queue that the UI drains on a timer.
/// </summary>
public sealed class SearchService : IDisposable
{
    private IntPtr _handle;
    private NativeMethods.MatchCallback? _callback; // pinned by field: the GC must not collect it mid-run
    private int _running;

    public SearchService()
    {
        _handle = NativeMethods.tf_create();
        if (_handle == IntPtr.Zero) throw new InvalidOperationException("tf_create failed");
    }

    public bool IsRunning => Volatile.Read(ref _running) != 0;

    /// <summary>Runs the search; matches are enqueued into <paramref name="sink"/> as they are found.</summary>
    public Task RunAsync(SearchRequest request, ConcurrentQueue<MatchItem> sink)
    {
        ObjectDisposedException.ThrowIf(_handle == IntPtr.Zero, this);
        if (Interlocked.Exchange(ref _running, 1) != 0) throw new InvalidOperationException("A search is already running.");

        return Task.Run(() =>
        {
            try
            {
                _callback = (file, line, text, _) =>
                    sink.Enqueue(new MatchItem(Marshal.PtrToStringUTF8(file) ?? "", line, Marshal.PtrToStringUTF8(text) ?? ""));

                var opts = new NativeMethods.TfOptions
                {
                    Root = request.Root,
                    Pattern = request.Pattern,
                    Extensions = request.Extensions,
                    MaxMatches = request.MaxMatches,
                    Threads = request.Threads,
                    UseRegex = request.UseRegex ? 1 : 0,
                    CaseSensitive = request.CaseSensitive ? 1 : 0,
                };

                if (NativeMethods.tf_run(_handle, ref opts, _callback, IntPtr.Zero) != 0)
                    throw new SearchException(Marshal.PtrToStringUTF8(NativeMethods.tf_last_error(_handle)) ?? "unknown error");
            }
            finally
            {
                GC.KeepAlive(_callback);
                Volatile.Write(ref _running, 0);
            }
        });
    }

    public void Cancel()
    {
        if (_handle != IntPtr.Zero) NativeMethods.tf_cancel(_handle);
    }

    public SearchStats GetStats()
    {
        if (_handle == IntPtr.Zero) return default;
        NativeMethods.tf_get_stats(_handle, out var s);
        return new SearchStats(s.FilesScanned, s.FilesSkipped, s.BytesRead, s.Matches, s.ElapsedMs, s.Running != 0);
    }

    public void Dispose()
    {
        var h = Interlocked.Exchange(ref _handle, IntPtr.Zero);
        if (h == IntPtr.Zero) return;
        NativeMethods.tf_cancel(h);
        while (IsRunning) Thread.Sleep(5); // never free the native engine while a scan is inside it
        NativeMethods.tf_destroy(h);
    }
}
