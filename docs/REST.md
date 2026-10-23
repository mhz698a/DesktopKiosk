# DesktopKiosk REST API

El cliente expone un servidor HTTP en el puerto **7085** para controlar el estado del kiosco.

## Seguridad

El servidor no inicia si `DESKTOPKIOSK_API_TOKEN` no está definido.

Por defecto escucha únicamente en `127.0.0.1`. Para aceptar conexiones desde otra máquina se debe definir explícitamente `DESKTOPKIOSK_HTTP_BIND` con una dirección IPv4 del equipo.

Cada petición requiere:

`Authorization: Bearer <DESKTOPKIOSK_API_TOKEN>`

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

Respuesta correcta:

`202 Accepted`

Ejemplo desde PowerShell:

```powershell
$env:DESKTOPKIOSK_API_TOKEN = "cambia-este-token"
$headers = @{ Authorization = "Bearer cambia-este-token" }

Invoke-WebRequest -Method POST -Uri "http://127.0.0.1:7085/api/kiosk/unlock" -Headers $headers
```

Para control remoto, además de configurar `DESKTOPKIOSK_HTTP_BIND`, el firewall de Windows debe permitir el puerto 7085 únicamente desde las máquinas autorizadas.

## WebView2

Los mensajes `postMessage` se validan contra:

1. El origen de la página cargada por DesktopKiosk.
2. Un objeto JSON que contenga una acción conocida.
3. Las acciones permitidas: `unlock`, `lock` y `close`.

Las acciones se publican al hilo de UI mediante mensajes `WM_APP`; el código HTTP no modifica directamente el estado de la ventana.

## Ventana de sesión

La clase `DesktopKioskSessionWindow` se registra una sola vez durante la inicialización de la aplicación. Esto permite crear y destruir la ventana de sesión repetidamente sin intentar registrar nuevamente la misma clase cada vez que se desbloquea el kiosco.

## Pantalla completa

La ventana principal se crea como `WS_POPUP` y se ajusta al rectángulo del monitor principal. WebView2 utiliza todo el área cliente disponible.

Ante `WM_DISPLAYCHANGE` o `WM_DPICHANGED`, la aplicación vuelve a calcular el rectángulo del monitor y reaplica la configuración de pantalla completa para mantener el kiosco ajustado a la configuración actual de pantalla.


## Registrar o actualizar la IP del servidor central

Enviar un POST a **http://<IP_DEL_CLIENTE>:7085/api/kiosk/receive** con el encabezado Content-Type: application/json y el token Bearer configurado en DESKTOPKIOSK_API_TOKEN. El cuerpo tiene esta estructura:

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

Una respuesta 202 Accepted confirma que el valor se guardó. Un JSON o una IP inválidos producen 400 Bad Request; un fallo al escribir el archivo produce 500 Internal Server Error; un token ausente o incorrecto produce 401 Unauthorized.

Ejemplo desde PowerShell:

~~~powershell
$headers = @{
    Authorization = "Bearer $env:DESKTOPKIOSK_API_TOKEN"
    "Content-Type" = "application/json"
}
$body = @{
    action = "receive"
    data = @{ server_control_ip = "192.168.1.10" }
} | ConvertTo-Json -Depth 3

Invoke-RestMethod -Method POST -Uri "http://127.0.0.1:7085/api/kiosk/receive" -Headers $headers -Body $body
~~~
