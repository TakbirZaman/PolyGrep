// Dependency-free test runner (so `dotnet run` works offline). Verifies struct layout + callbacks + cancel.
using System.Collections.Concurrent;
using ThreadForge.Interop;

int failed = 0, checks = 0;
void Check(bool ok, string what) { checks++; if (!ok) { failed++; Console.Error.WriteLine("FAIL: " + what); } }

var dir = Path.Combine(Path.GetTempPath(), "tf_cs_" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(Path.Combine(dir, "sub"));
try
{
    for (int i = 0; i < 20; i++)
        File.WriteAllText(Path.Combine(dir, "sub", $"f{i}.cs"), string.Join("\n", Enumerable.Range(0, 50).Select(l => l % 5 == 0 ? $"héllo World {l}" : $"filler {l}")));
    File.WriteAllText(Path.Combine(dir, "skip.log"), "héllo World");

    using var svc = new SearchService();
    var sink = new ConcurrentQueue<MatchItem>();

    await svc.RunAsync(new SearchRequest(dir, "WORLD", "cs", Threads: 4), sink);
    Check(sink.Count == 20 * 10, $"expected 200 matches, got {sink.Count}");
    Check(sink.All(m => m.Text.StartsWith("héllo World")), "UTF-8 round-trips through the callback");
    var st = svc.GetStats();
    Check(st.Matches == 200 && st.FilesScanned == 20 && !st.Running, "stats after run");

    sink.Clear();
    await svc.RunAsync(new SearchRequest(dir, @"^filler 4\d$", "cs", UseRegex: true, CaseSensitive: true), sink);
    Check(sink.Count == 20 * 8, $"regex expected 160, got {sink.Count}");

    sink.Clear();
    await svc.RunAsync(new SearchRequest(dir, "héllo", "", MaxMatches: 7), sink);
    Check(sink.Count == 7, $"max matches respected, got {sink.Count}");

    try { await svc.RunAsync(new SearchRequest(Path.Combine(dir, "nope"), "x"), sink); Check(false, "missing dir should throw"); }
    catch (SearchException ex) { Check(ex.Message.Contains("does not exist"), "error message marshalled: " + ex.Message); }

    // Cancel while a long scan is running.
    for (int i = 0; i < 150; i++) File.WriteAllText(Path.Combine(dir, "sub", $"big{i}.cs"), new string('a', 1_000_000) + "\nneedle\n");
    sink.Clear();
    var run = svc.RunAsync(new SearchRequest(dir, "needle", "cs", Threads: 2), sink);
    await Task.Delay(5);
    svc.Cancel();
    await run;
    Check(!svc.IsRunning, "not running after cancel");
    Check(svc.GetStats().FilesScanned < 170, "cancel stopped the scan early");
}
finally { Directory.Delete(dir, true); }

Console.WriteLine($"{checks} checks, {failed} failed");
return failed == 0 ? 0 : 1;
