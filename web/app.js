document.addEventListener('DOMContentLoaded', () => {
    const toggleBtn = document.querySelector('.password-toggle-btn');
    const passwordInput = document.querySelector('.password-input');

    if (toggleBtn && passwordInput) {
        toggleBtn.addEventListener('click', (e) => {
            e.preventDefault();
            const isPassword = passwordInput.type === 'password';
            
            // Cambia el tipo de input
            passwordInput.type = isPassword ? 'text' : 'password';
            
            // Alterna la clase en el botón para los SVGs
            toggleBtn.classList.toggle('is-visible', isPassword);
            
            // Opcional: Actualiza el lector de pantalla para accesibilidad
            toggleBtn.setAttribute('aria-label', isPassword ? 'Ocultar contraseña' : 'Mostrar contraseña');
        });
    } else {
        console.warn('No se encontró el botón de contraseña o el input en el DOM.');
    }
});


const unlock_buttons = document.querySelectorAll('.unlock');

unlock_buttons.forEach(boton => {
    boton.addEventListener('click', async (evento) => {
        // 1. Verificación de información completa
        if (!validarPanelActivo()) {
            return;
        }

        // 2. Preparación de los datos según el panel activo
        const activePanel = document.querySelector('.tab-panel.active');
        const panelId = activePanel.id;
        
        const inputs = activePanel.querySelectorAll('input, select');
        const formData = {};

        inputs.forEach((input, index) => {
            const key = input.id || input.name || `campo_${index}`;
            formData[key] = input.value.trim();
        });

        // Comprobar credenciales de control y validación de contraseña en inicio de sesión
        if (panelId === 'login-panel') {
            const usuario = formData['Nombres/Expediente'] || Object.values(formData)[0];
            const password = formData['Contraseña'] || Object.values(formData)[1];

            // Comandos especiales de administración
            if (usuario === '%admin%' && password === '.%admin%') {
                window.chrome.webview.postMessage({
                    action: "unlock",
                    data: formData
                });
                return;
            }

            if (usuario === '%end%' && password === '.%end%') {
                window.chrome.webview.postMessage({
                    action: "close",
                    data: formData
                });
                return;
            }

            // Validar que la contraseña sea exactamente un punto (.) seguido del usuario/expediente
            const passwordEsperada = '.' + usuario;
            if (password !== passwordEsperada) {
                await showAlertModal(`La contraseña debe ser un punto seguido de tu nombre o expediente (por ejemplo: ${passwordEsperada}).`);
                return;
            }
        }

        let actionType = "unlock";
        if (panelId === 'login-panel') actionType = "unlock_login";
        else if (panelId === 'internos-panel') actionType = "registro_alumno";
        else if (panelId === 'externos-panel') actionType = "registro_externo";

        window.chrome.webview.postMessage({
            action: actionType,
            data: formData
        });
    });
});


document.getElementById("close").addEventListener("click", () => {
    window.chrome.webview.postMessage({
        action: "close"
    });
});

document.addEventListener('DOMContentLoaded', () => {
    const selectFacultad = document.getElementById('facultad');

    const facultadesUAQ = [
        "Facultad de Artes",
        "Facultad de Ciencias Naturales",
        "Facultad de Ciencias Políticas y Sociales",
        "Facultad de Contaduría y Administración",
        "Facultad de Derecho",
        "Facultad de Enfermería",
        "Facultad de Filosofía",
        "Facultad de Informática",
        "Facultad de Ingeniería",
        "Facultad de Lenguas y Letras",
        "Facultad de Medicina",
        "Facultad de Psicología y Educación",
        "Facultad de Química"
    ];

    facultadesUAQ.forEach(facultad => {
        const option = document.createElement('option');
        option.value = facultad;
        option.textContent = facultad;
        selectFacultad.appendChild(option);
    });
});

document.addEventListener('DOMContentLoaded', () => {
    const selectUniversidad = document.getElementById('universidad-ext');

    const universidadesQueretaro = [
        // Públicas Principales
        "Instituto Tecnológico de Querétaro (ITQ)",
        "Universidad Tecnológica de Querétaro (UTEQ)",
        "Universidad Politécnica de Querétaro (UPQ)",
        "Universidad Aeronáutica en Querétaro (UNAQ)",
        "Universidad Nacional Autónoma de México (UNAM - Campus Juriquilla)",
        "Universidad Tecnológica de San Juan del Río (UTSJR)",
        "Universidad Politécnica de Santa Rosa Jáuregui (UPSRJ)",
        "Escuela Normal Superior de Querétaro – ENSQ",
        
        // Privadas Principales
        "Tecnológico de Monterrey (Campus Querétaro)",
        "Universidad Anáhuac (Sede Querétaro)",
        "Universidad Tecnológica de México (UNITEC)",
        "Universidad Marista de Querétaro",
        "Universidad Cuauhtémoc (Plantel Querétaro)",
        "Arkansas State University (Campus Querétaro)",
        "Universidad de Estudios Avanzados (UNEA)",
        "Universidad Tecmilenio",
        "Otra institución externa"
    ];

    if (selectUniversidad) {
        universidadesQueretaro.forEach(uni => {
            const option = document.createElement('option');
            option.value = uni;
            option.textContent = uni;
            selectUniversidad.appendChild(option);
        });
    }
});

function toggleUniversidad(selectObj) {
    const uniGroup = document.getElementById('uni-group');
    if (selectObj.value === 'estudiante') {
        uniGroup.style.display = 'block';
    } else {
        uniGroup.style.display = 'none';
    }
}

// Verificar que los campos visibles del panel activo no estén vacíos
function validarPanelActivo() {
    const activePanel = document.querySelector('.tab-panel.active');
    if (!activePanel) return true;

    const inputs = activePanel.querySelectorAll('input, select');
    let isValid = true;

    inputs.forEach(input => {
        // Si el elemento o su contenedor están ocultos (display: none), se omite de la validación
        if (input.offsetParent === null) {
            input.classList.remove('error');
            return;
        }

        if (!input.value.trim() || (input.tagName === 'SELECT' && input.selectedIndex === 0)) {
            input.classList.add('error');
            isValid = false;
        } else {
            input.classList.remove('error');
        }
    });

    if (!isValid) {
        alert('Por favor, completa todos los campos requeridos.');
    }

    return isValid;
}

// Comprobar si hay datos ingresados en un panel
function hasPanelData(panel) {
    const inputs = panel.querySelectorAll('input, select');
    for (let input of inputs) {
        if (input.tagName === 'SELECT') {
            if (input.selectedIndex > 0) return true;
        } else if (input.value.trim() !== '') {
            return true;
        }
    }
    return false;
}

// Limpiar campos y remover errores de un panel
function clearPanelFields(panel) {
    const inputs = panel.querySelectorAll('input, select');
    inputs.forEach(input => {
        if (input.tagName === 'SELECT') {
            input.selectedIndex = 0;
        } else {
            input.value = '';
        }
        input.classList.remove('error');
    });
}

// -------------------------------------------------------------

// Función para mostrar el modal de confirmación personalizado (reemplaza a confirm())
function showConfirmModal(message) {
    return new Promise((resolve) => {
        const modal = document.getElementById('confirm-modal');
        const msgElement = document.getElementById('confirm-message');
        const btnOk = document.getElementById('confirm-ok');
        const btnCancel = document.getElementById('confirm-cancel');

        msgElement.textContent = message;
        modal.classList.add('active');

        function cleanup(result) {
            modal.classList.remove('active');
            btnOk.removeEventListener('click', handleOk);
            btnCancel.removeEventListener('click', handleCancel);
            resolve(result);
        }

        const handleOk = () => cleanup(true);
        const handleCancel = () => cleanup(false);

        btnOk.addEventListener('click', handleOk, { once: true });
        btnCancel.addEventListener('click', handleCancel, { once: true });
    });
}

// Función para mostrar el modal personalizado de alerta
function showAlertModal(message) {
    return new Promise((resolve) => {
        const modal = document.getElementById('confirmAlertModal');
        const msgElement = document.getElementById('custom-modal-message');
        const btn = document.getElementById('custom-modal-btn');

        msgElement.textContent = message;
        modal.classList.add('active');

        const handleOk = () => {
            modal.classList.remove('active');
            btn.removeEventListener('click', handleOk);
            resolve();
        };

        btn.addEventListener('click', handleOk, { once: true });
    });
}

// Función mejorada para cambiar de pestaña utilizando el modal de confirmación
async function switchTab(evt, panelId) {
    const currentActivePanel = document.querySelector('.tab-panel.active');
    const targetPanel = document.getElementById(panelId);

    if (currentActivePanel === targetPanel) return;

    // Mostrar mensaje de confirmación si hay datos escritos en la pestaña actual
    if (currentActivePanel && hasPanelData(currentActivePanel)) {
        const confirmar = await showConfirmModal('Tienes datos ingresados sin guardar. ¿Estás seguro de cambiar de pestaña? Se perderá la información.');
        if (!confirmar) {
            return;
        }
    }

    // Limpiar los campos de la pestaña que se deja
    if (currentActivePanel) {
        clearPanelFields(currentActivePanel);
    }

    // Realizar el cambio de paneles y botones
    const panels = document.querySelectorAll('.tab-panel');
    panels.forEach(panel => panel.classList.remove('active'));

    const buttons = document.querySelectorAll('.tab-btn');
    buttons.forEach(btn => btn.classList.remove('active'));

    targetPanel.classList.add('active');
    evt.currentTarget.classList.add('active');
}