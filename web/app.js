const unlock_buttons = document.querySelectorAll('.unlock');

unlock_buttons.forEach(boton => {
  boton.addEventListener('click', (evento) => {
    window.chrome.webview.postMessage({
        action: "unlock"
    });
  });
});

document.getElementById("close").addEventListener("click", () => {
    window.chrome.webview.postMessage({
        action: "close"
    });
});

function switchTab(evt, panelId) {
    const panels = document.querySelectorAll('.tab-panel');
    panels.forEach(panel => panel.classList.remove('active'));

    const buttons = document.querySelectorAll('.tab-btn');
    buttons.forEach(btn => btn.classList.remove('active'));

    document.getElementById(panelId).classList.add('active');
    evt.currentTarget.classList.add('active');
}

function toggleUniversidad(selectObj) {
    const uniGroup = document.getElementById('uni-group');
    if (selectObj.value === 'estudiante') {
        uniGroup.style.display = 'block';
    } else {
        uniGroup.style.display = 'none';
    }
}