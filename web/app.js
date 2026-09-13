const browserState = document.querySelector("#browser-state");
const projectList = document.querySelector("#project-list");
const boardSelect = document.querySelector("#board-select");
const variantPicker = document.querySelector("#variant-picker");
const variantSelect = document.querySelector("#variant-select");
const installer = document.querySelector("#installer-button");
const espInstall = document.querySelector("#esp-install");
const picoInstall = document.querySelector("#pico-install");
const picoDownload = document.querySelector("#pico-download");
const picoSize = document.querySelector("#pico-size");
const picoSha = document.querySelector("#pico-sha");
const filters = [...document.querySelectorAll(".filter")];
const defaultBoardId = "esp32-c6-devkitc-1";
let projects = [];
let selectedProject = null;
let selectedTarget = null;
let selectedBuild = null;
let selectedBoardId = defaultBoardId;
let selectedVariantId = new URL(location.href).searchParams.get("variant") || "";
let activeFilter = "All";

const serialSupported = "serial" in navigator;

function updateInstallStatus() {
  const isUf2 = selectedBuild?.method === "uf2";
  browserState.classList.toggle("supported", isUf2 || serialSupported);
  browserState.classList.toggle("unsupported", !isUf2 && !serialSupported);
  browserState.querySelector("span:last-child").textContent = isUf2
    ? "UF2 download ready"
    : (serialSupported ? "Web Serial ready" : "Use Chrome or Edge");
}

updateInstallStatus();

function targetFor(project) {
  return project.targets.find((target) => target.id === selectedBoardId) || null;
}

function projectsForBoard() {
  return projects.filter((project) => targetFor(project));
}

function updateLocation(project) {
  const url = new URL(location.href);
  url.searchParams.set("board", selectedBoardId);
  if (selectedVariantId) url.searchParams.set("variant", selectedVariantId);
  else url.searchParams.delete("variant");
  url.hash = project.slug;
  history.replaceState(null, "", url);
}

function selectProject(project) {
  const target = targetFor(project);
  if (!target) return;
  selectedProject = project;
  selectedTarget = target;
  const variants = selectedTarget.variants || [];
  if (!variants.some((variant) => variant.id === selectedVariantId)) selectedVariantId = "";
  selectedBuild = variants.find((variant) => variant.id === selectedVariantId) || selectedTarget;
  variantPicker.hidden = variants.length === 0;
  variantSelect.replaceChildren(...[
    { id: "", name: selectedTarget.configuration_name || "Default build" },
    ...variants,
  ].map((variant) => {
    const option = document.createElement("option");
    option.value = variant.id;
    option.textContent = variant.name;
    return option;
  }));
  variantSelect.value = selectedVariantId;
  document.querySelector("#selected-index").textContent = `PROJECT ${String(projects.indexOf(project) + 1).padStart(2, "0")}`;
  document.querySelector("#selected-category").textContent = project.category;
  document.querySelector("#selected-name").textContent = project.name;
  document.querySelector("#selected-description").textContent = project.description;
  document.querySelector("#selected-chip").textContent = selectedTarget.name;
  document.querySelector("#selected-version").textContent = project.version;
  document.querySelector("#selected-hardware").textContent = selectedBuild.hardware || project.hardware;
  document.querySelector("#selected-setup").textContent = project.setup.summary;
  const hardwareGuide = document.querySelector("#hardware-guide");
  const wiring = document.querySelector("#selected-wiring");
  const connections = document.querySelector("#selected-connections");
  const warnings = document.querySelector("#selected-warnings");
  const parts = document.querySelector("#selected-parts");
  if (project.extra_hardware && selectedBuild.wiring && project.parts.length > 0) {
    hardwareGuide.hidden = false;
    wiring.src = selectedBuild.wiring.diagram;
    wiring.alt = `${project.name} ${selectedBuild.name || "default build"} wiring diagram for ${selectedTarget.name}`;
    connections.replaceChildren(...selectedBuild.wiring.connections.map((connection) => {
      const row = document.createElement("tr");
      [connection.from, connection.to, connection.wire].forEach((value) => {
        const cell = document.createElement("td");
        cell.textContent = value;
        row.append(cell);
      });
      return row;
    }));
    warnings.replaceChildren(...selectedBuild.wiring.warnings.map((warning) => {
      const item = document.createElement("li");
      item.textContent = warning;
      return item;
    }));
    parts.replaceChildren(...project.parts.map((part) => {
      const item = document.createElement("li");
      const description = document.createElement("span");
      const name = document.createElement("strong");
      name.textContent = `${part.quantity}× ${part.name} · ${part.required ? "Required" : "Optional"}`;
      const specification = document.createElement("small");
      specification.textContent = part.specification;
      description.append(name, specification);
      const link = document.createElement("a");
      link.href = part.url;
      link.target = "_blank";
      link.rel = "noopener noreferrer";
      link.textContent = "Amazon search";
      item.append(description, link);
      return item;
    }));
  } else {
    hardwareGuide.hidden = true;
    wiring.removeAttribute("src");
    wiring.alt = "";
    connections.replaceChildren();
    warnings.replaceChildren();
    parts.replaceChildren();
  }
  const features = document.querySelector("#selected-features");
  features.replaceChildren(...project.features.map((feature) => {
    const item = document.createElement("li");
    item.textContent = feature;
    return item;
  }));
  const isUf2 = selectedBuild.method === "uf2";
  espInstall.hidden = isUf2;
  picoInstall.hidden = !isUf2;
  if (isUf2) {
    picoDownload.href = selectedBuild.download;
    picoDownload.download = `${project.slug}-${project.version}-${selectedTarget.id}.uf2`;
    picoSize.textContent = `${new Intl.NumberFormat().format(selectedBuild.size)} bytes`;
    picoSha.textContent = selectedBuild.sha256;
    installer.removeAttribute("manifest");
  } else {
    picoDownload.removeAttribute("href");
    picoDownload.removeAttribute("download");
    picoSize.textContent = "";
    picoSha.textContent = "";
    installer.setAttribute("manifest", selectedBuild.manifest);
    installer.manifest = selectedBuild.manifest;
  }
  updateInstallStatus();
  updateLocation(project);
  document.querySelectorAll(".project-row").forEach((row) => {
    const selected = row.dataset.slug === project.slug;
    row.classList.toggle("selected", selected);
    row.setAttribute("aria-pressed", String(selected));
  });
}

function makeProjectRow(project) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = "project-row";
  button.dataset.slug = project.slug;
  button.setAttribute("aria-pressed", "false");

  const number = document.createElement("span");
  number.className = "row-number";
  number.textContent = String(projects.indexOf(project) + 1).padStart(2, "0");
  const copy = document.createElement("span");
  copy.className = "row-copy";
  const name = document.createElement("strong");
  name.textContent = project.name;
  const description = document.createElement("small");
  description.textContent = project.description;
  copy.append(name, description);
  const category = document.createElement("span");
  category.className = `category category-${project.category.toLowerCase()}`;
  category.textContent = project.category;
  button.append(number, copy, category);
  button.addEventListener("click", () => selectProject(project));
  return button;
}

function renderProjects() {
  const compatible = projectsForBoard();
  const visible = activeFilter === "All" ? compatible : compatible.filter((project) => project.category === activeFilter);
  document.querySelector("#project-count").textContent = String(compatible.length);
  if (visible.length > 0 && !visible.includes(selectedProject)) selectedProject = visible[0];
  projectList.replaceChildren(...visible.map(makeProjectRow));
  if (selectedProject && visible.includes(selectedProject)) selectProject(selectedProject);
}

function configureBoards() {
  const boards = new Map();
  projects.forEach((project) => project.targets.forEach((target) => {
    if (!boards.has(target.id)) boards.set(target.id, target);
  }));
  boardSelect.replaceChildren(...[...boards.values()].map((board) => {
    const option = document.createElement("option");
    option.value = board.id;
    option.textContent = board.name;
    return option;
  }));
  const requestedBoard = new URL(location.href).searchParams.get("board");
  selectedBoardId = boards.has(requestedBoard) ? requestedBoard : (boards.has(defaultBoardId) ? defaultBoardId : boards.keys().next().value);
  boardSelect.value = selectedBoardId;
}

variantSelect.addEventListener("change", () => {
  selectedVariantId = variantSelect.value;
  if (selectedProject) selectProject(selectedProject);
});

boardSelect.addEventListener("change", () => {
  selectedBoardId = boardSelect.value;
  if (selectedProject && !targetFor(selectedProject)) selectedProject = null;
  renderProjects();
});

filters.forEach((button) => button.addEventListener("click", () => {
  activeFilter = button.dataset.filter;
  filters.forEach((filter) => {
    const active = filter === button;
    filter.classList.toggle("active", active);
    filter.setAttribute("aria-pressed", String(active));
  });
  renderProjects();
}));

fetch("./projects.json")
  .then((response) => {
    if (!response.ok) throw new Error(`Project catalog returned ${response.status}`);
    return response.json();
  })
  .then((catalog) => {
    projects = catalog;
    configureBoards();
    const requested = location.hash.slice(1);
    selectedProject = projects.find((project) => project.slug === requested && targetFor(project)) || projectsForBoard()[0];
    renderProjects();
  })
  .catch((error) => {
    console.error("Project catalog unavailable", error);
    projectList.textContent = "The firmware catalog could not be loaded. Refresh and try again.";
  });
