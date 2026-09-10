const browserState = document.querySelector("#browser-state");
const projectList = document.querySelector("#project-list");
const boardSelect = document.querySelector("#board-select");
const installer = document.querySelector("#installer-button");
const filters = [...document.querySelectorAll(".filter")];
const defaultBoardId = "esp32-c6-devkitc-1";
let projects = [];
let selectedProject = null;
let selectedTarget = null;
let selectedBoardId = defaultBoardId;
let activeFilter = "All";

const serialSupported = "serial" in navigator;
browserState.classList.add(serialSupported ? "supported" : "unsupported");
browserState.querySelector("span:last-child").textContent = serialSupported ? "Web Serial ready" : "Use Chrome or Edge";

function targetFor(project) {
  return project.targets.find((target) => target.id === selectedBoardId) || null;
}

function projectsForBoard() {
  return projects.filter((project) => targetFor(project));
}

function updateLocation(project) {
  const url = new URL(location.href);
  url.searchParams.set("board", selectedBoardId);
  url.hash = project.slug;
  history.replaceState(null, "", url);
}

function selectProject(project) {
  const target = targetFor(project);
  if (!target) return;
  selectedProject = project;
  selectedTarget = target;
  document.querySelector("#selected-index").textContent = `PROJECT ${String(projects.indexOf(project) + 1).padStart(2, "0")}`;
  document.querySelector("#selected-category").textContent = project.category;
  document.querySelector("#selected-name").textContent = project.name;
  document.querySelector("#selected-description").textContent = project.description;
  document.querySelector("#selected-chip").textContent = selectedTarget.name;
  document.querySelector("#selected-version").textContent = project.version;
  document.querySelector("#selected-hardware").textContent = project.hardware;
  document.querySelector("#selected-setup").textContent = project.setup.summary;
  const features = document.querySelector("#selected-features");
  features.replaceChildren(...project.features.map((feature) => {
    const item = document.createElement("li");
    item.textContent = feature;
    return item;
  }));
  installer.setAttribute("manifest", selectedTarget.manifest);
  installer.manifest = selectedTarget.manifest;
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
