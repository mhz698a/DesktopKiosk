# DesktopKiosk REST API

El cliente expone un servidor HTTP en el puerto **7085** para controlar el estado del kiosco.

## Seguridad

El servidor no inicia si `DESKTOPKIOSK_API_TOKEN` no está definido.

Por defecto escucha únicamente en `127.0.0.1`. Para aceptar conexiones desde otra máquina se debe definir explícitamente `DESKTOPKIOSK_HTTP_BIND` con una dirección IPv4 del equipo.

Cada petición requiere:

`Authorization: Bearer <DESKTOPKIOSK_API_TOKEN>`

No se acepta desbloqueo anónimo.

## Endpoints

Todos son `POST`:

- `/api/kiosk/unlock`
- `/api/kiosk/lock`
- `/api/kiosk/close`

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

## Pantalla completa

La ventana principal se crea como `WS_POPUP` y se ajusta al rectángulo del monitor principal. WebView2 utiliza todo el área cliente disponible.
