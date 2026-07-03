import { ModelViewer } from "./dataset_model_viewer.js";


const CATALOG_URL = "./dataset_catalog.json";
const SELECTION_KEY = "nextlife_dataset_explorer_selection_v1";
const ANNOTATION_KEY = "nextlife_dataset_explorer_annotations_v1";
const SECURITY_LEVELS = ["Original", "Transparent", "Suffisant", "Confidentiel"];
const SECURITY_ORDER = new Map(SECURITY_LEVELS.map((level, index) => [level, index]));

const state = {
  catalog: null,
  selectedObjectIds: new Set(),
  annotations: {},
  mode: "object",
  scope: "security",
  sceneMode: "neutral",
  activeObjectId: "",
  activeDistortionKey: "",
  activeVariant: null,
  loadToken: 0,
};

const els = {
  catalogCount: document.getElementById("catalogCount"),
  objectSelectionCount: document.getElementById("objectSelectionCount"),
  objectSearch: document.getElementById("objectSearch"),
  classFilter: document.getElementById("classFilter"),
  sceneFilter: document.getElementById("sceneFilter"),
  objectList: document.getElementById("objectList"),
  selectVisibleBtn: document.getElementById("selectVisibleBtn"),
  clearVisibleBtn: document.getElementById("clearVisibleBtn"),
  modeButtons: [...document.querySelectorAll(".mode-btn")],
  scopeSelect: document.getElementById("scopeSelect"),
  sceneMode: document.getElementById("sceneMode"),
  axisLabel: document.getElementById("axisLabel"),
  axisSelect: document.getElementById("axisSelect"),
  sortSelect: document.getElementById("sortSelect"),
  resultTitle: document.getElementById("resultTitle"),
  resultCount: document.getElementById("resultCount"),
  resultList: document.getElementById("resultList"),
  currentObject: document.getElementById("currentObject"),
  currentDistortion: document.getElementById("currentDistortion"),
  currentMeta: document.getElementById("currentMeta"),
  securityChoices: document.getElementById("securityChoices"),
  clearRatingBtn: document.getElementById("clearRatingBtn"),
  annotationNote: document.getElementById("annotationNote"),
  loadingState: document.getElementById("loadingState"),
  status: document.getElementById("status"),
  exportCsvBtn: document.getElementById("exportCsvBtn"),
  exportJsonBtn: document.getElementById("exportJsonBtn"),
};

const viewer = new ModelViewer(document.getElementById("modelCanvas"));

function readJsonStorage(key, fallback) {
  try {
    return JSON.parse(localStorage.getItem(key) || "") || fallback;
  } catch {
    return fallback;
  }
}

function saveSelection() {
  localStorage.setItem(SELECTION_KEY, JSON.stringify([...state.selectedObjectIds]));
}

function saveAnnotations() {
  localStorage.setItem(ANNOTATION_KEY, JSON.stringify(state.annotations));
}

function setStatus(message) {
  els.status.textContent = message;
}

function normalizeSearch(text) {
  return String(text || "").normalize("NFD").replace(/[\u0300-\u036f]/g, "").toLowerCase();
}

function objectMatchesFilters(object) {
  if (els.classFilter.value && object.imagenet_class !== els.classFilter.value) return false;
  if (els.sceneFilter.value && object.scene_id !== els.sceneFilter.value) return false;
  const query = normalizeSearch(els.objectSearch.value.trim());
  if (!query) return true;
  return normalizeSearch(`${object.object_name} ${object.object_id} ${object.imagenet_class}`).includes(query);
}

function visibleObjects() {
  return state.catalog.objects.filter(objectMatchesFilters);
}

function selectedVisibleObjects() {
  return visibleObjects().filter((object) => state.selectedObjectIds.has(object.object_id));
}

function variantMatchesScope(variant) {
  if (variant.is_original) return true;
  if (state.scope === "security") return variant.is_core_security;
  if (state.scope === "canonical") return variant.is_canonical;
  return true;
}

function scopedVariants() {
  return state.catalog.variants.filter(
    (variant) => variantMatchesScope(variant) && state.selectedObjectIds.has(variant.object_id)
  );
}

function annotationKey(variant = state.activeVariant) {
  return variant ? `${variant.trial_id}::${state.sceneMode}` : "";
}

function annotationFor(variant) {
  return state.annotations[`${variant.trial_id}::${state.sceneMode}`] || null;
}

function fillFilter(select, values, firstLabel) {
  select.replaceChildren();
  const first = document.createElement("option");
  first.value = "";
  first.textContent = firstLabel;
  select.append(first);
  values.forEach((value) => {
    const option = document.createElement("option");
    option.value = value;
    option.textContent = value;
    select.append(option);
  });
}

function renderObjectList() {
  const objects = visibleObjects();
  els.objectList.replaceChildren();
  objects.forEach((object) => {
    const row = document.createElement("div");
    row.className = `object-row${object.object_id === state.activeObjectId ? " active" : ""}`;

    const checkbox = document.createElement("input");
    checkbox.type = "checkbox";
    checkbox.checked = state.selectedObjectIds.has(object.object_id);
    checkbox.setAttribute("aria-label", `Sélectionner ${object.object_name}`);
    checkbox.addEventListener("change", () => {
      if (checkbox.checked) state.selectedObjectIds.add(object.object_id);
      else state.selectedObjectIds.delete(object.object_id);
      saveSelection();
      renderObjectList();
      refreshAxesAndResults(true);
    });

    const open = document.createElement("button");
    open.type = "button";
    open.className = "object-open";
    const name = document.createElement("strong");
    name.textContent = object.object_name;
    const meta = document.createElement("span");
    meta.textContent = `${object.imagenet_class} · ${object.scene_id}`;
    open.append(name, meta);
    open.addEventListener("click", () => {
      state.selectedObjectIds.add(object.object_id);
      state.activeObjectId = object.object_id;
      state.mode = "object";
      saveSelection();
      updateModeButtons();
      renderObjectList();
      refreshAxesAndResults(true);
    });

    const count = document.createElement("span");
    count.className = "variant-count";
    count.textContent = String(object.core_security_variant_count);
    count.title = `${object.variant_count} variantes présentes`;
    row.append(checkbox, open, count);
    els.objectList.append(row);
  });

  const selected = state.selectedObjectIds.size;
  els.objectSelectionCount.textContent = `${selected} sélectionnés · ${objects.length} visibles`;
  if (!objects.length) {
    const empty = document.createElement("div");
    empty.className = "empty-state";
    empty.textContent = "Aucun objet ne correspond aux filtres.";
    els.objectList.append(empty);
  }
}

function availableDistortions() {
  const keys = new Set(scopedVariants().map((variant) => variant.distortion_key));
  return state.catalog.distortions.filter((distortion) => keys.has(distortion.distortion_key));
}

function updateModeButtons() {
  els.modeButtons.forEach((button) => button.classList.toggle("selected", button.dataset.mode === state.mode));
}

function fillAxisSelect() {
  els.axisSelect.replaceChildren();
  if (state.mode === "object") {
    els.axisLabel.textContent = "Objet";
    selectedVisibleObjects().forEach((object) => {
      const option = document.createElement("option");
      option.value = object.object_id;
      option.textContent = `${object.imagenet_class} · ${object.object_name}`;
      els.axisSelect.append(option);
    });
    const available = [...els.axisSelect.options].map((option) => option.value);
    if (!available.includes(state.activeObjectId)) state.activeObjectId = available[0] || "";
    els.axisSelect.value = state.activeObjectId;
  } else {
    els.axisLabel.textContent = "Distorsion";
    availableDistortions().forEach((distortion) => {
      const option = document.createElement("option");
      option.value = distortion.distortion_key;
      option.textContent = `${distortion.distortion_label} (${distortion.object_count})`;
      els.axisSelect.append(option);
    });
    const available = [...els.axisSelect.options].map((option) => option.value);
    if (!available.includes(state.activeDistortionKey)) {
      state.activeDistortionKey = available.find((key) => key !== "original") || available[0] || "";
    }
    els.axisSelect.value = state.activeDistortionKey;
  }
}

function resultVariants() {
  const variants = scopedVariants().filter((variant) => {
    if (state.mode === "object") return variant.object_id === state.activeObjectId;
    return variant.distortion_key === state.activeDistortionKey;
  });
  const sort = els.sortSelect.value;
  variants.sort((left, right) => {
    if (sort === "rating") {
      const leftRank = SECURITY_ORDER.get(annotationFor(left)?.security_level) ?? 99;
      const rightRank = SECURITY_ORDER.get(annotationFor(right)?.security_level) ?? 99;
      if (leftRank !== rightRank) return leftRank - rightRank;
    }
    if (sort === "family") {
      const familyOrder = `${left.distortion_family} ${left.distortion_profile}`.localeCompare(
        `${right.distortion_family} ${right.distortion_profile}`,
        "fr"
      );
      if (familyOrder) return familyOrder;
    }
    const leftName = state.mode === "object" ? left.distortion_label : left.object_name;
    const rightName = state.mode === "object" ? right.distortion_label : right.object_name;
    return leftName.localeCompare(rightName, "fr");
  });
  return variants;
}

function renderResultList(loadFirst = false) {
  const variants = resultVariants();
  els.resultList.replaceChildren();
  els.resultCount.textContent = `${variants.length} éléments`;
  els.resultTitle.textContent = state.mode === "object" ? "Distorsions de l’objet" : "Objets avec cette distorsion";

  variants.forEach((variant) => {
    const row = document.createElement("button");
    row.type = "button";
    row.className = `result-row${variant.trial_id === state.activeVariant?.trial_id ? " active" : ""}`;
    const main = document.createElement("span");
    main.className = "result-main";
    const title = document.createElement("strong");
    const subtitle = document.createElement("span");
    if (state.mode === "object") {
      title.textContent = variant.distortion_label;
      subtitle.textContent = `${variant.distortion_modality} · ${variant.variant_id}`;
    } else {
      title.textContent = variant.object_name;
      subtitle.textContent = `${variant.imagenet_class} · ${variant.scene_id}`;
    }
    main.append(title, subtitle);

    const annotation = annotationFor(variant);
    const rating = document.createElement("span");
    rating.className = `rating-pill${annotation?.security_level ? " rated" : ""}`;
    rating.textContent = annotation?.security_level || "Non classé";
    row.append(main, rating);
    row.addEventListener("click", () => loadVariant(variant));
    els.resultList.append(row);
  });

  if (!variants.length) {
    const empty = document.createElement("div");
    empty.className = "empty-state";
    empty.textContent = state.selectedObjectIds.size
      ? "Aucune variante dans ce périmètre."
      : "Sélectionne au moins un objet.";
    els.resultList.append(empty);
    clearCurrentVariant();
  } else if (loadFirst || !variants.some((variant) => variant.trial_id === state.activeVariant?.trial_id)) {
    loadVariant(variants.find((variant) => !variant.is_original) || variants[0]);
  }
}

function refreshAxesAndResults(loadFirst = false) {
  fillAxisSelect();
  renderResultList(loadFirst);
}

function renderSecurityChoices() {
  els.securityChoices.replaceChildren();
  SECURITY_LEVELS.forEach((level) => {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = level;
    button.dataset.level = level;
    button.addEventListener("click", () => setClassification(level));
    els.securityChoices.append(button);
  });
}

function updateAnnotationControls() {
  const annotation = state.activeVariant ? state.annotations[annotationKey()] : null;
  [...els.securityChoices.children].forEach((button) => {
    button.classList.toggle("selected", button.dataset.level === annotation?.security_level);
    button.disabled = !state.activeVariant;
  });
  els.annotationNote.disabled = !state.activeVariant;
  els.annotationNote.value = annotation?.note || "";
}

function writeCurrentAnnotation(level, note) {
  if (!state.activeVariant) return;
  const key = annotationKey();
  if (!level && !note.trim()) {
    delete state.annotations[key];
  } else {
    state.annotations[key] = {
      trial_id: state.activeVariant.trial_id,
      condition: state.sceneMode,
      security_level: level || "",
      note: note.trim(),
      updated_at: new Date().toISOString(),
    };
  }
  saveAnnotations();
  updateAnnotationControls();
  renderResultList(false);
}

function setClassification(level) {
  const current = state.annotations[annotationKey()] || {};
  writeCurrentAnnotation(level, current.note || els.annotationNote.value);
  setStatus(`Classement enregistré : ${level}.`);
}

function clearCurrentVariant() {
  state.activeVariant = null;
  els.currentObject.textContent = "Aucun objet";
  els.currentDistortion.textContent = "";
  els.currentMeta.textContent = "";
  updateAnnotationControls();
}

async function loadVariant(variant) {
  state.activeVariant = variant;
  state.activeObjectId = variant.object_id;
  const token = ++state.loadToken;
  els.currentObject.textContent = `${variant.imagenet_class} · ${variant.object_name}`;
  els.currentDistortion.textContent = variant.distortion_label;
  els.currentMeta.textContent = `${variant.distortion_modality} · ${variant.scene_id}`;
  els.loadingState.hidden = false;
  setStatus("");
  updateAnnotationControls();
  renderResultList(false);
  renderObjectList();

  try {
    const realScene = await viewer.setScene(variant.scene_id, state.sceneMode === "context");
    await viewer.load(variant.model, variant);
    if (token !== state.loadToken) return;
    if (state.sceneMode === "context" && !realScene) setStatus("Scène procédurale utilisée en secours.");
  } catch (error) {
    if (token === state.loadToken) setStatus(`Chargement impossible : ${error.message}`);
  } finally {
    if (token === state.loadToken) els.loadingState.hidden = true;
  }
}

function csvEscape(value) {
  const text = String(value ?? "");
  return /[",\n\r]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text;
}

function download(name, content, type) {
  const blob = new Blob([content], { type });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = name;
  link.click();
  URL.revokeObjectURL(url);
}

function annotatedRows() {
  const byTrial = new Map(state.catalog.variants.map((variant) => [variant.trial_id, variant]));
  return Object.values(state.annotations).flatMap((annotation) => {
    const variant = byTrial.get(annotation.trial_id);
    if (!variant) return [];
    return [{
      updated_at: annotation.updated_at,
      trial_id: variant.trial_id,
      condition: annotation.condition,
      object_selected: state.selectedObjectIds.has(variant.object_id),
      object_id: variant.object_id,
      object_name: variant.object_name,
      imagenet_class: variant.imagenet_class,
      scene_id: variant.scene_id,
      variant_id: variant.variant_id,
      distortion_modality: variant.distortion_modality,
      distortion_family: variant.distortion_family,
      distortion_profile: variant.distortion_profile,
      is_canonical: variant.is_canonical,
      is_core_security: variant.is_core_security,
      security_level: annotation.security_level,
      note: annotation.note,
      manifest: variant.manifest,
    }];
  }).sort((left, right) => left.object_name.localeCompare(right.object_name, "fr"));
}

function exportCsv() {
  const rows = annotatedRows();
  if (!rows.length) {
    setStatus("Aucun classement à exporter.");
    return;
  }
  const headers = Object.keys(rows[0]);
  const content = [headers.join(","), ...rows.map((row) => headers.map((key) => csvEscape(row[key])).join(","))].join("\n");
  download("nextlife_visual_security_classification.csv", `${content}\n`, "text/csv;charset=utf-8");
  setStatus(`${rows.length} classements exportés.`);
}

function exportJson() {
  const payload = {
    schema_version: 1,
    exported_at: new Date().toISOString(),
    selected_object_ids: [...state.selectedObjectIds],
    classifications: annotatedRows(),
  };
  download("nextlife_visual_security_classification.json", JSON.stringify(payload, null, 2), "application/json");
  setStatus(`${payload.classifications.length} classements exportés.`);
}

async function loadCatalog() {
  const response = await fetch(CATALOG_URL, { cache: "no-store" });
  if (!response.ok) throw new Error("dataset_catalog.json introuvable");
  state.catalog = await response.json();
  const savedSelection = readJsonStorage(SELECTION_KEY, null);
  state.selectedObjectIds = new Set(
    Array.isArray(savedSelection) ? savedSelection : state.catalog.objects.map((object) => object.object_id)
  );
  state.annotations = readJsonStorage(ANNOTATION_KEY, {});
  state.activeObjectId = state.catalog.objects[0]?.object_id || "";

  const classes = [...new Set(state.catalog.objects.map((object) => object.imagenet_class))].sort((a, b) => a.localeCompare(b, "fr"));
  const scenes = [...new Set(state.catalog.objects.map((object) => object.scene_id))].sort((a, b) => a.localeCompare(b, "fr"));
  fillFilter(els.classFilter, classes, "Toutes les classes");
  fillFilter(els.sceneFilter, scenes, "Toutes les scènes");
  const counts = state.catalog.counts;
  els.catalogCount.textContent = `${counts.objects} objets · ${counts.core_security_variants} sécurité · ${counts.canonical_distorted_variants} canoniques`;
  renderObjectList();
  refreshAxesAndResults(true);
}

els.objectSearch.addEventListener("input", () => {
  renderObjectList();
  refreshAxesAndResults(true);
});
[els.classFilter, els.sceneFilter].forEach((select) => select.addEventListener("change", () => {
  renderObjectList();
  refreshAxesAndResults(true);
}));
els.selectVisibleBtn.addEventListener("click", () => {
  visibleObjects().forEach((object) => state.selectedObjectIds.add(object.object_id));
  saveSelection();
  renderObjectList();
  refreshAxesAndResults(true);
});
els.clearVisibleBtn.addEventListener("click", () => {
  visibleObjects().forEach((object) => state.selectedObjectIds.delete(object.object_id));
  saveSelection();
  renderObjectList();
  refreshAxesAndResults(true);
});
els.modeButtons.forEach((button) => button.addEventListener("click", () => {
  state.mode = button.dataset.mode;
  updateModeButtons();
  refreshAxesAndResults(true);
}));
els.scopeSelect.addEventListener("change", () => {
  state.scope = els.scopeSelect.value;
  state.activeDistortionKey = "";
  refreshAxesAndResults(true);
});
els.axisSelect.addEventListener("change", () => {
  if (state.mode === "object") state.activeObjectId = els.axisSelect.value;
  else state.activeDistortionKey = els.axisSelect.value;
  renderObjectList();
  renderResultList(true);
});
els.sortSelect.addEventListener("change", () => renderResultList(false));
els.sceneMode.addEventListener("change", () => {
  state.sceneMode = els.sceneMode.value;
  updateAnnotationControls();
  renderResultList(false);
  if (state.activeVariant) loadVariant(state.activeVariant);
});
els.clearRatingBtn.addEventListener("click", () => writeCurrentAnnotation("", ""));
els.annotationNote.addEventListener("change", () => {
  const current = state.annotations[annotationKey()] || {};
  writeCurrentAnnotation(current.security_level || "", els.annotationNote.value);
});
els.exportCsvBtn.addEventListener("click", exportCsv);
els.exportJsonBtn.addEventListener("click", exportJson);

renderSecurityChoices();
loadCatalog().catch((error) => setStatus(error.message));

function animate() {
  viewer.render();
  requestAnimationFrame(animate);
}
requestAnimationFrame(animate);
