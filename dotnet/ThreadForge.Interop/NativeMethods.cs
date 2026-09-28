using System.Runtime.InteropServices;

namespace ThreadForge.Interop;

/// <summary>P/Invoke bindings for the C ABI in core/include/threadforge/capi.h (all strings UTF-8).</summary>
internal static class NativeMethods
{
    private const string Lib = "threadforge";

    // Layout must mirror `tf_options` exactly (pointers, u64, then 32-bit fields).
    [StructLayout(LayoutKind.Sequential)]
    internal struct TfOptions
    {
        [MarshalAs(UnmanagedType.LPUTF8Str)] public string Root;
        [MarshalAs(UnmanagedType.LPUTF8Str)] public string Pattern;
        [MarshalAs(UnmanagedType.LPUTF8Str)] public string? Extensions;
        public ulong MaxMatches;
        public uint Threads;
        public int UseRegex;
        public int CaseSensitive;
        public int CountWords;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct TfStats
    {
        public ulong FilesScanned;
        public ulong FilesSkipped;
        public ulong BytesRead;
        public ulong Matches;
        public ulong ElapsedMs;
        public int Running;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void MatchCallback(IntPtr file, ulong line, IntPtr text, IntPtr user);

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr tf_create();
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern void tf_destroy(IntPtr h);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern int tf_run(IntPtr h, ref TfOptions opts, MatchCallback cb, IntPtr user);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern void tf_cancel(IntPtr h);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern void tf_get_stats(IntPtr h, out TfStats stats);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr tf_last_error(IntPtr h);
}
