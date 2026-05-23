using System;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.VisualStudio.Shell;
using Task = System.Threading.Tasks.Task;

namespace CloseCrab
{
    [PackageRegistration(UseManagedResourcesOnly = true, AllowsBackgroundLoading = true)]
    [Guid("a1b2c3d4-e5f6-7890-abcd-ef1234567890")]
    [ProvideMenuResource("Menus.ctmenu", 1)]
    [ProvideToolWindow(typeof(ChatToolWindow))]
    public sealed class CloseCrabPackage : AsyncPackage
    {
        protected override async Task InitializeAsync(CancellationToken cancellationToken, IProgress<ServiceProgressData> progress)
        {
            await JoinableTaskFactory.SwitchToMainThreadAsync(cancellationToken);
            await ChatCommand.InitializeAsync(this);
            await ExplainCommand.InitializeAsync(this);
            await FixCommand.InitializeAsync(this);
            await ReviewCommand.InitializeAsync(this);
        }
    }
}
