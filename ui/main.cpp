#include <windows.h>

#include "MainForm.h"

#using < System.dll>
#using < System.Windows.Forms.dll>

using namespace System;
using namespace System::IO;
using namespace System::Reflection;
using namespace System::Windows::Forms;

[STAThreadAttribute] int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    String ^ startupDir = Path::GetDirectoryName(Assembly::GetExecutingAssembly()->Location);
    if (!String::IsNullOrWhiteSpace(startupDir))
    {
        Environment::CurrentDirectory = startupDir;
    }

    Application::EnableVisualStyles();
    Application::SetCompatibleTextRenderingDefault(false);
    Application::Run(gcnew fvpyuki::MainForm());
    return 0;
}