document.getElementById("unlock").addEventListener("click", () => {
    window.chrome.webview.postMessage({
        action: "unlock"
    });
});

document.getElementById("lock").addEventListener("click", () => {
    window.chrome.webview.postMessage({
        action: "lock"
    });
});

document.getElementById("close").addEventListener("click", () => {
    window.chrome.webview.postMessage({
        action: "close"
    });
});