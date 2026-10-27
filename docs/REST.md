# DesktopKiosk REST API

El cliente expone un servidor HTTP en el puerto **7085** para controlar el estado del kiosco.

## Seguridad

El token `UAQinformatizacion7085` viene integrado en el ejecutable. Al iniciar, DesktopKiosk crea `%APPDATA%\DesktopKiosk\settings.ini` si hace falta y guarda el token bajo `[security]` y `api_token`. Si encuentra una clave vacía o un token distinto al incluido en la versión ejecutable, lo reemplaza automáticamente. No se necesita configurar variables de entorno ni editar el archivo manualmente.

Por defecto escucha en `0.0.0.0`, es decir, en todas las interfaces IPv4. Para limitarlo a una interfaz concreta, define `DESKTOPKIOSK_HTTP_BIND` con la dirección IPv4 correspondiente. El cliente no crea ni modifica reglas del Firewall de Windows: esa configuración queda a cargo del instalador. El instalador debe establecer una regla de entrada para el ejecutable y TCP 7085 antes del primer inicio, habilitando solo los perfiles necesarios y restringiendo las direcciones remotas cuando corresponda.

Cada petición POST requiere:

`Authorization: Bearer UAQinformatizacion7085`

Los nombres de los encabezados HTTP se procesan sin distinguir mayúsculas y minúsculas, conforme a HTTP.

No se acepta desbloqueo anónimo.

## Protección del servidor HTTP

El procesamiento de clientes utiliza un conjunto fijo de **8 workers** y una cola limitada a **32 conexiones pendientes**. Cada socket cliente tiene un timeout de lectura y escritura de **5 segundos**.

Esto evita que una conexión que permanezca abierta sin completar una petición bloquee indefinidamente el servidor y limita el consumo de recursos ante múltiples conexiones simultáneas.

Las conexiones que superen la capacidad de la cola reciben una respuesta `503 Service Unavailable`.

## Endpoints

Todos son `POST`:

- `/api/kiosk/unlock`
- `/api/kiosk/lock`
- `/api/kiosk/close`
- `/api/kiosk/receive`

Las rutas de control y recepción devuelven `202 Accepted` al completarse.

Ejemplo desde PowerShell:

```powershell
$headers = @{ Authorization = "Bearer UAQinformatizacion7085" }

Invoke-WebRequest -Method POST -Uri "http://127.0.0.1:7085/api/kiosk/unlock" -Headers $headers
```

Para control remoto, configura `DESKTOPKIOSK_HTTP_BIND` si quieres limitar la interfaz. La regla de entrada correspondiente debe instalarse desde el instalador y limitar el acceso a los equipos autorizados.

## WebView2

Los mensajes `postMessage` se validan contra:

1. El origen de la página cargada por DesktopKiosk.
2. Un objeto JSON que contenga una acción conocida.
3. Las acciones permitidas: `unlock`, `lock` y `close`.

Las acciones se publican al hilo de UI mediante mensajes `WM_APP`; el código HTTP no modifica directamente el estado de la ventana. WebView2 deshabilita de forma nativa DevTools y sus menús contextuales predeterminados; la página también cancela el menú contextual y los atajos de teclado habituales para abrir herramientas de desarrollo.

## Bloqueo por inactividad

Mientras el kiosco está desbloqueado, la aplicación consulta la última entrada de teclado o mouse de la sesión de Windows con `GetLastInputInfo`. A los **180 segundos** sin actividad muestra un aviso no bloqueante que indica que quedan dos minutos. Si la inactividad continúa hasta **300 segundos**, el kiosco se bloquea y vuelve a la pantalla de acceso. Cualquier nueva entrada de teclado o mouse reinicia el cómputo y oculta el aviso si ya se había mostrado.


La clase `DesktopKioskSessionWindow` se registra una sola vez durante la inicialización de la aplicación. Esto permite crear y destruir la ventana de sesión repetidamente sin intentar registrar nuevamente la misma clase cada vez que se desbloquea el kiosco.

## Pantalla completa

La ventana principal se crea como `WS_POPUP` y se ajusta al rectángulo del monitor principal. WebView2 utiliza todo el área cliente disponible.

Ante `WM_DISPLAYCHANGE` o `WM_DPICHANGED`, la aplicación vuelve a calcular el rectángulo del monitor y reaplica la configuración de pantalla completa para mantener el kiosco ajustado a la configuración actual de pantalla.


## Registrar o actualizar la IP del servidor central

Enviar un POST a **http://<IP_DEL_CLIENTE>:7085/api/kiosk/receive** con el encabezado Content-Type: application/json y el token Bearer `UAQinformatizacion7085`. El cuerpo tiene esta estructura:

~~~json
{
  "action": "receive",
  "data": {
    "server_control_ip": "192.168.1.10"
  }
}
~~~

El cliente valida que server_control_ip sea una dirección IPv4. Cada petición válida sobrescribe el valor anterior en:

**%APPDATA%\DesktopKiosk\settings.ini**

Si el directorio o el archivo no existen, se crean. El valor se guarda bajo la sección [network] y la clave server_control_ip:

~~~ini
[network]
server_control_ip=192.168.1.10
~~~

Cada recepción válida que se guarda correctamente genera un diálogo de confirmación en WebView2, incluso si la IP recibida es igual a la que ya estaba configurada. Una respuesta 202 Accepted confirma que el valor se guardó. Un JSON o una IP inválidos producen 400 Bad Request; un fallo al escribir el archivo produce 500 Internal Server Error; un token incorrecto produce 401 Unauthorized.

Ejemplo desde PowerShell:

~~~powershell
$headers = @{
    Authorization = "Bearer UAQinformatizacion7085"
    "Content-Type" = "application/json"
}
$body = @{
    action = "receive"
    data = @{ server_control_ip = "192.168.1.10" }
} | ConvertTo-Json -Depth 3

Invoke-RestMethod -Method POST -Uri "http://127.0.0.1:7085/api/kiosk/receive" -Headers $headers -Body $body
~~~

## Token de la instalación

El ejecutable usa el token integrado `UAQinformatizacion7085`. En cada inicio lo compara con el valor de `%APPDATA%\DesktopKiosk\settings.ini` y escribe el valor integrado si la clave falta o contiene otro token:

~~~ini
[security]
api_token=UAQinformatizacion7085
~~~

El token es común para todas las instalaciones de esta versión y no debe considerarse un secreto fuerte. El instalador debe limitar el puerto 7085 en el Firewall de Windows a los equipos autorizados.

## Comunicación con DesktopKioskCenter

El inicio de sesión y los registros no pasan por un endpoint HTTP adicional de DesktopKiosk. El JavaScript de WebView2 envía una solicitud mediante `window.chrome.webview.postMessage`; C++ lee `[network] server_control_ip` de `%APPDATA%\DesktopKiosk\settings.ini`, ejecuta la petición HTTP al servidor central y publica la respuesta de vuelta por el mismo canal WebView2. No se añade una ruta HTTP local para las solicitudes de login o registro.

DesktopKiosk envía un heartbeat mediante `POST /api/v1/health` cada segundo, desde un hilo en segundo plano y con un timeout breve para no bloquear la interfaz. El estado enviado es `1` cuando el kiosco está disponible, `2` cuando hay una sesión activa y `3` cuando esa sesión supera 180 segundos sin teclado o mouse. `time_string` contiene el tiempo transcurrido de la sesión en formato `HH:MM:SS`. El cliente no envía su IP en el JSON: el centro identifica el equipo mediante la IP de origen de la conexión. El estado `0` (Sin conexión) lo asigna el centro cuando pasan 4 segundos sin recibir heartbeats. Las operaciones de login y registro se envían directamente a sus rutas funcionales, sin una petición GET de health previa. La interfaz mantiene un timeout de 20 segundos para esas operaciones. La API central actual no requiere un token Bearer para estas rutas; DesktopKiosk envía el encabezado `X-Kiosk-Name`.

Rutas de la API central implementadas en `DesktopKioskCenter/api_server.py`:

- Inicio de sesión: `POST /api/v1/auth-client-pc/login`
- Registro de alumno: `POST /api/v1/users/register/alumno`
- Registro de externo: `POST /api/v1/users/register/externo`
- Heartbeat/estado del kiosco: `POST /api/v1/health`

Las solicitudes POST usan el cuerpo JSON que espera la API central, con las claves de primer nivel `action` y `data`. Por ejemplo:
En inicio de sesión, `data.usuario` contiene el correo electrónico; los alumnos internos también pueden usar su expediente como alternativa. Los nombres no se usan para identificar cuentas. El formulario de registro de alumnos internos solicita email, y el centro valida que sea único entre todas las categorías de usuarios.


~~~json
{
  "action": "unlock_login",
  "data": {
    "usuario": "alumno@example.com",
    "contrasena": ".alumno@example.com"
  }
}
~~~

La API devuelve `unlocked` y `message`. DesktopKiosk desbloquea el kiosco cuando el login es aceptado o cuando un registro nuevo termina correctamente (HTTP satisfactorio y `unlocked: true`). Tras registrar, la interfaz vuelve a la pestaña de inicio de sesión, limpia los campos y desbloquea sin exigir un segundo login. Las respuestas negativas muestran `message` con `showAlertModal()` y conservan la pestaña y los datos del formulario; los errores y registros duplicados no desbloquean el kiosco.

Errores de conectividad:

- Si no hay una IP IPv4 válida en `[network] server_control_ip`, C++ comunica: `Este cliente no conoce el servidor central, contacte con el administrador de este equipo`.
- Si la operación de login o registro no obtiene respuesta dentro del timeout o el servidor central deja de responder, C++ comunica: `No se encontro el servidor central, contacte con el administrador de este equipo`.

Las alertas se renderizan con `showAlertModal()`; no se utiliza `alert()` nativo de WebView2.


## Bloqueo de atajos del kiosco

DesktopKiosk instala un hook de teclado de bajo nivel mientras se ejecuta. El hook solo suprime combinaciones cuando la ventana en primer plano pertenece al proceso de DesktopKiosk; en el resto de aplicaciones deja pasar los eventos. No requiere privilegios de administrador. Se bloquean la tecla Windows, Ctrl+Esc y Alt+Tab. Alt+F4 se intercepta aparte mediante el evento nativo `AcceleratorKeyPressed` del controlador WebView2 y se marca como manejado.

Ctrl+Shift+Esc se deja pasar de forma explícita. Ctrl+Alt+Supr tampoco se intercepta: es una secuencia segura de Windows que una aplicación de usuario no puede bloquear mediante este hook. Si el hook no puede instalarse, DesktopKiosk no inicia la interfaz del kiosco.
