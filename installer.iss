[Setup]
AppName=DesktopKiosk
AppVersion=1.0
DefaultDirName={commonpf}\DesktopKiosk
DefaultGroupName=DesktopKiosk
PrivilegesRequired=admin
OutputDir=.\Output
OutputBaseFilename=Installer_DesktopKiosk
Compression=lzma
SolidCompression=yes

[Files]
; 1. Tu ejecutable unificado por Enigma, el cual ya saldrá firmado desde el script .bat
Source: ".\build\Release\DesktopKioskClient.exe"; DestDir: "{app}"; Flags: ignoreversion

; 2. El certificado público (.cer) necesario para instalar la confianza en el kiosk
Source: ".\kiosko_cert.cer"; DestDir: "{tmp}"; Flags: deleteafterinstall

; 3. Todas las dependencias adicionales recolectadas en la carpeta de distribución
Source: ".\dist\DesktopKioskCenter\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; Acceso directo en el Menú de Inicio para todos los usuarios
Name: "{group}\DesktopKiosk"; Filename: "{app}\DesktopKioskClient.exe"

[Run]
; Instalar la clave pública en el almacén del sistema
Filename: "certutil.exe"; Parameters: "-addstore -f ""Root"" ""{tmp}\kiosko_cert.cer"""; StatusMsg: "Configurando certificado de confianza (Raíz)..."; Flags: runhidden
Filename: "certutil.exe"; Parameters: "-addstore -f ""TrustedPublisher"" ""{tmp}\kiosko_cert.cer"""; StatusMsg: "Configurando certificado de confianza (Editor)..."; Flags: runhidden

; Configurar la regla del Firewall para tu puerto de red de datos
Filename: "netsh.exe"; Parameters: "advfirewall firewall add rule name=""DesktopKiosk_In"" dir=in action=allow protocol=TCP localport=7085 program=""{app}\DesktopKioskClient.exe"" enable=yes"; StatusMsg: "Configurando reglas de red en el Firewall..."; Flags: runhidden

; Crear la tarea programada para que el quiosco inicie solo al prender la PC con máximos privilegios
Filename: "schtasks.exe"; Parameters: "/Create /TN ""DesktopKiosk_Startup"" /TR ""'{app}\DesktopKioskClient.exe'"" /SC ONLOGON /RL HIGHEST /F"; StatusMsg: "Configurando inicio automático del Kiosko..."; Flags: runhidden

[UninstallRun]
; Eliminando la tarea programada
Filename: "schtasks.exe"; Parameters: "/Delete /TN ""DesktopKiosk_Startup"" /F"; Flags: runhidden; RunOnceId: "RemoveKioskTask"

; Eliminando reglas de firewall
Filename: "netsh.exe"; Parameters: "advfirewall firewall delete rule name=""DesktopKiosk_In"""; Flags: runhidden; RunOnceId: "RemoveKioskFirewall"

; Eliminando certificados del programa
Filename: "certutil.exe"; Parameters: "-delstore ""Root"" ""DesktopKiosk For UAQ"""; Flags: runhidden; RunOnceId: "RemoveKioskRootCert"
Filename: "certutil.exe"; Parameters: "-delstore ""TrustedPublisher"" ""DesktopKiosk For UAQ"""; Flags: runhidden; RunOnceId: "RemoveKioskPubCert"