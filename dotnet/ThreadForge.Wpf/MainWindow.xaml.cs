using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using ThreadForge.Interop;
using ThreadForge.Wpf.ViewModels;

namespace ThreadForge.Wpf;

public partial class MainWindow : Window
{
    private readonly MainViewModel _vm = new();

    public MainWindow()
    {
        InitializeComponent();
        DataContext = _vm;
        Closed += (_, _) => _vm.Dispose();
    }

    private void OnRowDoubleClick(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (sender is DataGrid { SelectedItem: MatchItem m })
            Process.Start(new ProcessStartInfo(m.File) { UseShellExecute = true });
    }
}
