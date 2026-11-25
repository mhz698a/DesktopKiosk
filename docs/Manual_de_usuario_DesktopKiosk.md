# Manual de usuario — DesktopKiosk
**Versión:** 1.0  
**Fecha:** 25 de noviembre de 2026  
**Público:** usuarios de los equipos de consulta y personal autorizado para operar el kiosco.

## 1. ¿Qué es DesktopKiosk?
DesktopKiosk es el programa que se ejecuta en cada computadora habilitada como kiosco. Presenta una pantalla de acceso, solicita la validación del usuario al centro de control y, cuando el acceso es autorizado, permite utilizar el equipo durante una sesión supervisada.

El programa trabaja junto con DesktopKioskCenter. El centro registra usuarios, administra los equipos y recibe los eventos de acceso. DesktopKiosk no sustituye el registro institucional ni permite crear una cuenta por sí solo.

## 2. Antes de iniciar
- El equipo debe estar encendido y DesktopKiosk debe mostrar la pantalla de inicio de sesión.
- La computadora debe tener conexión con la red donde opera DesktopKioskCenter.
- El usuario debe estar registrado previamente en el centro de control.
- El identificador debe corresponder al tipo de cuenta: correo electrónico, expediente o número de empleado.

Cuando el programa indique que no encuentra el servidor central, el equipo no podrá validar el acceso. Se debe informar al responsable del centro de cómputo; no se debe intentar modificar la configuración de red del equipo.

## 3. Iniciar sesión
1. En el campo «Correo electrónico / Expediente / No. de empleado», escribe el identificador con el que se registró la cuenta.
2. En «Contraseña», escribe un punto seguido inmediatamente del mismo identificador.
3. Pulsa «Iniciar sesión».
4. Espera la respuesta del servidor central. El kiosco se desbloquea únicamente cuando el servidor acepta el acceso.

**Ejemplos del formato de contraseña:**
- Expediente 202400123: **.202400123**
- Número de empleado 654321: **.654321**
- Correo persona@ejemplo.edu: **.persona@ejemplo.edu**

En esta versión, la contraseña no es una clave independiente elegida por el usuario: se forma con un punto y el identificador de la cuenta. La validación de la cuenta se realiza contra los registros existentes en DesktopKioskCenter.

Si los datos son incorrectos, revisa que no haya espacios adicionales y que el identificador corresponda al registrado. Si el mensaje indica que la cuenta no existe, solicita al operador que verifique el registro en DesktopKioskCenter.

## 4. Tipos de usuario e identificadores
| Tipo de cuenta | Identificador de acceso |
|---|---|
| Alumno interno | Correo electrónico o expediente |
| Docente | Correo electrónico o número de empleado |
| Administrativo | Correo electrónico o número de empleado |
| Usuario externo / público en general | Correo electrónico |

El nombre y los apellidos sirven para identificar visualmente a la persona en los registros, pero no se utilizan como identificador para iniciar sesión.

## 5. Registro de usuarios
El código del cliente incluye formularios de registro para alumnos, docentes y administrativos, y usuarios externos. Sin embargo, en la versión distribuida actual, la barra de pestañas que permite acceder a esos formularios está oculta. Por eso, el usuario común no puede abrirlos desde la pantalla del kiosco.

Para solicitar un registro, proporciona al operador del centro de cómputo la información correspondiente:
- **Alumno interno:** nombres, apellidos, correo electrónico, facultad y expediente.
- **Docente o administrativo:** tipo de personal, nombres, apellidos, correo electrónico, facultad o unidad de adscripción y número de empleado.
- **Usuario externo:** nombres, apellidos, correo electrónico y tipo de usuario. Algunos tipos requieren seleccionar una universidad de procedencia.

El operador registra la cuenta desde la pestaña correspondiente de DesktopKioskCenter. Después de confirmar el registro, el usuario puede volver a la pantalla del kiosco e iniciar sesión.

## 6. Durante una sesión
Cuando el acceso se acepta, la pantalla de inicio del kiosco se oculta y aparece un pequeño indicador de sesión en la esquina inferior derecha. El indicador muestra el tiempo transcurrido y ofrece el botón «Cerrar sesión».

Al terminar de utilizar la computadora, pulsa «Cerrar sesión». El kiosco vuelve a su pantalla de acceso y queda disponible para otra persona.

## 7. Bloqueo por inactividad
DesktopKiosk supervisa la actividad del teclado y del mouse durante una sesión:
- Aproximadamente a los **3 minutos** sin actividad muestra un aviso de inactividad.
- Aproximadamente a los **5 minutos** sin actividad bloquea la sesión y regresa a la pantalla inicial.

El aviso es preventivo. Mover el mouse o pulsar una tecla antes de que se cumpla el tiempo de bloqueo indica que el usuario sigue activo y cancela el aviso. Los tiempos se cuentan durante una sesión desbloqueada.

## 8. Menú administrativo del kiosco
Existe un menú local de administración para personal autorizado. No forma parte del flujo normal del usuario.

El menú ofrece estas acciones:
- **Desbloquear:** habilita el equipo sin asociar el inicio a una cuenta validada por el flujo habitual.
- **Cerrar Kiosko:** finaliza el programa de kiosco después de solicitar confirmación.

Las credenciales locales de administración deben consultarse únicamente mediante la acción «Credencial del administrador» de DesktopKioskCenter y entregarse solo al personal autorizado. No se incluyen en este manual ni deben compartirse con usuarios comunes.

## 9. Problemas frecuentes
| Situación | Qué hacer |
|---|---|
| El servidor central no responde | Comprueba que el equipo esté conectado a la red institucional y avisa al operador. |
| El identificador no es reconocido | Verifica el correo, expediente o número de empleado y solicita al centro revisar el registro. |
| La contraseña es rechazada | Comprueba que comience con un punto y que después tenga exactamente el identificador utilizado. |
| El kiosco vuelve a la pantalla inicial | Puede haber ocurrido el bloqueo automático por inactividad. Inicia sesión de nuevo. |
| No aparece el formulario de registro | Solicita el alta al operador de DesktopKioskCenter; las pestañas de registro no están visibles en la interfaz actual. |

## 10. Uso responsable
- Cierra la sesión al terminar; no dejes una sesión abierta para la siguiente persona.
- No compartas tus datos de acceso.
- No intentes cerrar o alterar el software de kiosco.
- Reporta al personal responsable los mensajes de error o los problemas de conexión.