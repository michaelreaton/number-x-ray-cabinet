param(
  [string]$WindowTitle = "Number X-Ray Workbench",
  [string]$OutputPath = "artifacts/native-workbench-window.png",
  [int]$WaitSeconds = 10
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;

public static class NativeWindowCapture {
  [DllImport("user32.dll")]
  public static extern bool SetProcessDPIAware();

  [DllImport("user32.dll")]
  public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);

  [DllImport("user32.dll")]
  public static extern bool GetWindowRect(IntPtr hWnd, out Rect lpRect);

  [DllImport("user32.dll")]
  public static extern bool SetForegroundWindow(IntPtr hWnd);

  [DllImport("user32.dll")]
  public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

  [DllImport("user32.dll")]
  public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);

  [StructLayout(LayoutKind.Sequential)]
  public struct Rect {
    public int Left;
    public int Top;
    public int Right;
    public int Bottom;
  }
}
"@

[NativeWindowCapture]::SetProcessDPIAware() | Out-Null

$deadline = (Get-Date).AddSeconds($WaitSeconds)
$handle = [IntPtr]::Zero
while ((Get-Date) -lt $deadline) {
  $handle = [NativeWindowCapture]::FindWindow($null, $WindowTitle)
  if ($handle -eq [IntPtr]::Zero) {
    $process = Get-Process |
      Where-Object { $_.MainWindowTitle -eq $WindowTitle -and $_.MainWindowHandle -ne 0 } |
      Select-Object -First 1
    if ($process) {
      $handle = $process.MainWindowHandle
    }
  }
  if ($handle -ne [IntPtr]::Zero) { break }
  Start-Sleep -Milliseconds 250
}

if ($handle -eq [IntPtr]::Zero) {
  throw "Window titled '$WindowTitle' was not found."
}

[NativeWindowCapture]::ShowWindow($handle, 9) | Out-Null
[NativeWindowCapture]::SetForegroundWindow($handle) | Out-Null
Start-Sleep -Milliseconds 350

$rect = New-Object NativeWindowCapture+Rect
if (-not [NativeWindowCapture]::GetWindowRect($handle, [ref]$rect)) {
  throw "Unable to read native window bounds."
}

$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -lt 1 -or $height -lt 1) {
  throw "Invalid native window bounds: ${width}x${height}."
}

$resolvedOutput = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
$outputDir = Split-Path -Parent $resolvedOutput
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$bitmap = New-Object System.Drawing.Bitmap($width, $height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try {
  $hdc = $graphics.GetHdc()
  try {
    $printed = [NativeWindowCapture]::PrintWindow($handle, $hdc, 2)
  } finally {
    $graphics.ReleaseHdc($hdc)
  }
  if (-not $printed) {
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
  }
  $bitmap.Save($resolvedOutput, [System.Drawing.Imaging.ImageFormat]::Png)
} finally {
  $graphics.Dispose()
  $bitmap.Dispose()
}

Write-Output "Captured '$WindowTitle' at ${width}x${height} from physical bounds to $resolvedOutput"
