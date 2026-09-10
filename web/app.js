const browserState = document.querySelector("#browser-state");
const projectList = document.querySelector("#project-list");
const installer = document.querySelector("#installer-button");
const filters = [...document.querySelectorAll(".filter")];
let projects = [];
let selectedProject = null;
let activeFilter = "All";

const serialSupported = "serial" in navigator;
browserState.classList.add(serialSupported ? "supported" : "unsupported");
browserState.querySelector("span:last-child").textContent = serialSupported ? "Web Serial ready" : "Use Chrome or Edge";

function selectProject(project) {
  selectedProject = project;
  document.querySelector("#selected-index").textContent = `PROJECT ${String(projects.indexOf(project) + 1).padStart(2, "0")}`;
  document.querySelector("#selected-category").textContent = project.category;
  document.querySelector("#selected-name").textContent = project.name;
  document.querySelector("#selected-description").textContent = project.description;
  document.querySelector("#selected-chip").textContent = project.chip;
  document.querySelector("#selected-version").textContent = project.version;
  const features = document.querySelector("#selected-features");
  features.replaceChildren(...project.features.map((feature) => {
    const item = document.createElement("li");
    item.textContent = feature;
    return item;
  }));
  installer.manifest = selectedProject.manifest;
  location.hash = project.slug;
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
  const visible = activeFilter === "All" ? projects : projects.filter((project) => project.category === activeFilter);
  if (visible.length > 0 && !visible.includes(selectedProject)) selectedProject = visible[0];
  projectList.replaceChildren(...visible.map(makeProjectRow));
  if (selectedProject) selectProject(selectedProject);
}

filters.forEach((button) => button.addEventListener("click", () => {
  activeFilter = button.dataset.filter;
  filters.forEach((filter) => filter.classList.toggle("active", filter === button));
  renderProjects();
}));

fetch("./projects.json")
  .then((response) => {
    if (!response.ok) throw new Error(`Project catalog returned ${response.status}`);
    return response.json();
  })
  .then((catalog) => {
    projects = catalog;
    document.querySelector("#project-count").textContent = String(projects.length);
    const requested = location.hash.slice(1);
    selectedProject = projects.find((project) => project.slug === requested) || projects[0];
    renderProjects();
  })
  .catch((error) => {
    console.error("Project catalog unavailable", error);
    projectList.textContent = "The firmware catalog could not be loaded. Refresh and try again.";
  });
