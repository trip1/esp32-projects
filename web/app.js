const browserState = document.querySelector("#browser-state");
const supported = "serial" in navigator;

browserState.classList.add(supported ? "supported" : "unsupported");
browserState.querySelector("span:last-child").textContent = supported
  ? "Web Serial ready"
  : "Use Chrome or Edge";

fetch("./projects.json")
  .then((response) => {
    if (!response.ok) throw new Error(`Project catalog returned ${response.status}`);
    return response.json();
  })
  .then((projects) => {
    const scanner = projects.find((project) => project.slug === "ble-mqtt-scanner");
    if (scanner) document.querySelector("#release-version").textContent = scanner.version;
  })
  .catch((error) => console.warn("Project catalog unavailable", error));
