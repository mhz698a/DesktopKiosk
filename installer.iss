#define AppName "DesktopKiosk"
#define AppVersion "1.0"
#define AppExeName "DesktopKiosk.exe"
#define BuildOutputDir "build\\Release"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
DefaultDirName={autopf}\\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\\{#AppExeName}
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir=Output
OutputBaseFilename=Installer_DesktopKiosk
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableProgramGroupPage=yes

[Files]
; CMake genera DesktopKiosk.exe y windeployqt coloca aquí las DLL y plugins de Qt.
; Se incluye todo el directorio Release para conservar la estructura de ejecución.
Source: "{#BuildOutputDir}\\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "*.pdb,*.ilk,*.lib,*.exp"

; Certificado público usado por este cliente.
Source: "kiosko_cert.cer"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\\DesktopKiosk"; Filename: "{app}\\{#AppExeName}"

[Run]
; Instalar el certificado en los almacenes de confianza del equipo.
Filename: "{sys}\\certutil.exe"; Parameters: "-addstore -f ""Root"" ""{tmp}\\kiosko_cert.cer"""; StatusMsg: "Instalando certificado raíz..."; Flags: runhidden waituntilterminated
Filename: "{sys}\\certutil.exe"; Parameters: "-addstore -f ""TrustedPublisher"" ""{tmp}\\kiosko_cert.cer"""; StatusMsg: "Instalando certificado de editor..."; Flags: runhidden waituntilterminated

; Permitir conexiones entrantes al servidor REST local del kiosco.
Filename: "{sys}\\netsh.exe"; Parameters: "advfirewall firewall add rule name=""DesktopKiosk_In"" dir=in action=allow protocol=TCP localport=7085 program=""{app}\\{#AppExeName}"" enable=yes"; StatusMsg: "Configurando el Firewall..."; Flags: runhidden waituntilterminated

; Ejecutar el cliente al iniciar sesión con el nivel de privilegios más alto.
Filename: "{sys}\\schtasks.exe"; Parameters: "{code:GetStartupTaskParameters}"; StatusMsg: "Configurando el inicio automático..."; Flags: runhidden waituntilterminated

[UninstallRun]
Filename: "{sys}\\schtasks.exe"; Parameters: "/Delete /TN ""DesktopKiosk_Startup"" /F"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveKioskTask"
Filename: "{sys}\\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""DesktopKiosk_In"""; Flags: runhidden waituntilterminated; RunOnceId: "RemoveKioskFirewall"

; El sujeto del certificado es exactamente "DesktopKiosk For UAQ".
Filename: "{sys}\\certutil.exe"; Parameters: "-delstore ""Root"" ""DesktopKiosk For UAQ"""; Flags: runhidden waituntilterminated; RunOnceId: "RemoveKioskRootCert"
Filename: "{sys}\\certutil.exe"; Parameters: "-delstore ""TrustedPublisher"" ""DesktopKiosk For UAQ"""; Flags: runhidden waituntilterminated; RunOnceId: "RemoveKioskPublisherCert"

[Code]
function GetStartupTaskParameters(Param: string): string;
begin
  ; schtasks necesita comillas literales alrededor de la ruta del ejecutable
  ; almacenada en la acción de la tarea. Los caracteres \" escapan esas
  ; comillas para el parser de argumentos de Windows.
  Result := '/Create /TN "DesktopKiosk_Startup" /TR "\\\"' +
    ExpandConstant('{app}\\DesktopKiosk.exe') +
    '\\\"" /SC ONLOGON /RL HIGHEST /F';
end;
