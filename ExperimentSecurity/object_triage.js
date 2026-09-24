import { ModelViewer } from "./dataset_model_viewer.js?v=20260918-3";
import { ObjectReview, REVIEW_STATUSES } from "./object_review.js?v=2";

const $ = id => document.getElementById(id);
const viewer = new ModelViewer($("modelCanvas"));
let catalog, activeId = "", variant = null, loadToken = 0, loadQueue = Promise.resolve(), originalSeen = false;
const notify = message => { $("status").textContent = message; };
function download(name, text, type) {
  const url = URL.createObjectURL(new Blob([text], {type}));
  const link = document.createElement("a"); link.href = url; link.download = name; link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
const review = new ObjectReview({container: $("objectReview"), filter: $("reviewFilter"), notify, download,
  onChange: () => renderList(), onOriginal: () => showOriginal(),
  onSaved: () => { if ($("autoNext").checked) navigate(1); },
  getContext: () => ({protocol: "nextlife_object_triage_v1", inspected_trial_id: variant?.trial_id || null,
    inspected_distortion: variant?.distortion_key || null, original_loaded_this_session: originalSeen}),
});
const saveReview = review.save.bind(review);
review.save = () => {
  if (review.el.reviewStatus.value === "retained" && !originalSeen) {
    notify("Afficher et vérifier l’original avant de retenir cet objet."); return;
  }
  saveReview();
};
function visible() {
  const query = $("search").value.toLocaleLowerCase().trim();
  return (catalog?.objects || []).filter(o =>
    (!query || `${o.object_name} ${o.object_id} ${o.imagenet_class}`.toLocaleLowerCase().includes(query)) &&
    (!$("classFilter").value || o.imagenet_class === $("classFilter").value) &&
    (!$("reviewFilter").value || review.status(o.object_id) === $("reviewFilter").value) &&
    (!$("auditFilter").value || review.audit.get(o.object_id)?.issues.length));
}
function renderList() {
  $("objectList").replaceChildren();
  const objects = visible();
  for (const o of objects) {
    const button = document.createElement("button"); button.classList.toggle("active", o.object_id === activeId);
    button.textContent = o.object_name;
    const label = document.createElement("small");
    label.textContent = `${o.imagenet_class} · ${REVIEW_STATUSES[review.status(o.object_id)]}${review.audit.get(o.object_id)?.issues.length ? " · ⚠ audit" : ""}`;
    button.append(label); button.onclick = () => openObject(o.object_id); $("objectList").append(button);
  }
  $("progress").textContent = `${objects.length} visibles · ${(catalog?.objects || []).filter(o => review.status(o.object_id) !== "pending").length}/${catalog?.objects.length || 0} examinés`;
}
function navigate(step) {
  const objects = visible(), index = objects.findIndex(o => o.object_id === activeId);
  const next = objects[index < 0 ? 0 : index + step];
  if (next) openObject(next.object_id); else notify("Fin de la liste filtrée.");
}
function openObject(id) {
  activeId = id; originalSeen = false; review.show(id); renderList();
  $("variantSelect").replaceChildren();
  const variants = catalog.variants.filter(v => v.object_id === id).sort((a,b) => Number(b.is_original)-Number(a.is_original));
  for (const v of variants) $("variantSelect").add(new Option(v.is_original ? "Original — référence" : `${v.distortion_family} · ${v.distortion_profile} · ${v.variant_id}`, v.trial_id));
  showOriginal();
}
function showOriginal() {
  const original = catalog.variants.find(v => v.object_id === activeId && v.is_original);
  if (original) { $("variantSelect").value = original.trial_id; loadVariant(original); }
  else { $("variantSelect").selectedIndex = -1; loadVariant(null); }
}
async function loadVariant(next) {
  variant = next; const token = ++loadToken;
  $("modelCanvas").style.visibility = "hidden";
  $("loading").hidden = !next;
  $("displayed").textContent = next ? (next.is_original ? "Original — aucune distorsion appliquée" : `Variante : ${next.distortion_family} · profil ${next.distortion_profile} · ${next.distortion_modality}. Décision de tri sur l’objet source.`) : "Original absent du catalogue : tri possible, rétention bloquée.";
  notify("");
  if (!next) return;
  const start = performance.now();
  try {
    const task = loadQueue.catch(() => {}).then(async () => {
      if (token !== loadToken) return;
      await viewer.setScene(next.scene_id, false);
      if (token !== loadToken) return;
      await viewer.load(next.model, next);
    });
    loadQueue = task; await task;
    if (token !== loadToken) return;
    $("modelCanvas").style.visibility = "visible";
    if (next.is_original) originalSeen = true;
    notify(`Modèle chargé en ${((performance.now()-start)/1000).toFixed(1)} s. Inspecter plusieurs angles.`);
  } catch (error) { if (token === loadToken) notify(`Chargement impossible : ${error.message}`); }
  finally { if (token === loadToken) $("loading").hidden = true; }
}
function choose(status) {
  if (!activeId) return;
  review.el.reviewStatus.value = status;
  if (["repair", "excluded"].includes(status)) { review.el.reviewReason.focus(); notify("Préciser le motif, puis enregistrer la décision."); }
  else review.save();
}
function turn() {
  if (!variant) return;
  const offset = viewer.camera.position.clone().sub(viewer.controls.target);
  const x = offset.x; offset.x = offset.z; offset.z = -x;
  viewer.camera.position.copy(viewer.controls.target).add(offset); viewer.controls.update();
}
$("previous").onclick = () => navigate(-1); $("next").onclick = () => navigate(1);
$("original").onclick = showOriginal; $("turn").onclick = turn;
$("resetView").onclick = () => { if (variant) loadVariant(variant); };
$("variantSelect").onchange = () => loadVariant(catalog.variants.find(v => v.trial_id === $("variantSelect").value));
document.querySelectorAll("[data-status]").forEach(button => { button.onclick = () => choose(button.dataset.status); });
for (const id of ["search", "classFilter", "reviewFilter", "auditFilter"]) $(id).addEventListener(id === "search" ? "input" : "change", () => { renderList(); const first = visible()[0]; if (first) openObject(first.object_id); });
document.addEventListener("keydown", event => {
  if (event.ctrlKey || event.altKey || event.metaKey || event.repeat) return;
  const editing = event.target.matches("input, textarea, select, [contenteditable]");
  if (editing) return;
  const actions = {"1":()=>choose("retained"), "2":()=>choose("repair"), "3":()=>choose("excluded"), "4":()=>choose("pending"), ArrowLeft:()=>navigate(-1), ArrowRight:()=>navigate(1), o:showOriginal, v:turn, Enter:()=>review.save()};
  if (actions[event.key]) { event.preventDefault(); actions[event.key](); }
});
review.el.reviewReason.addEventListener("keydown", event => { if (event.key === "Enter") { event.preventDefault(); review.save(); } });
async function init() {
  const response = await fetch("./dataset_catalog.json", {cache:"no-store"});
  if (!response.ok) throw new Error("Catalogue absent : lancer build_dataset_catalog.py depuis la racine du projet.");
  catalog = await response.json(); review.objects = catalog.objects;
  for (const label of [...new Set(catalog.objects.map(o => o.imagenet_class))].sort()) $("classFilter").add(new Option(label, label));
  renderList(); if (catalog.objects.length) openObject(catalog.objects[0].object_id);
}
init().catch(error => notify(error.message));
function animate() { viewer.render(); requestAnimationFrame(animate); } requestAnimationFrame(animate);
