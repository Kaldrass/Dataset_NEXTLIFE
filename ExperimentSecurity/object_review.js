const KEY = "nextlife_object_review_v1";
export const REVIEW_STATUSES = {pending: "À examiner", retained: "Retenu", repair: "À réparer", excluded: "Écarté de l’expérience"};
const REASONS = ["", "texture absente", "géométrie incomplète", "chargement impossible", "échelle / orientation", "classe ambiguë", "label incorrect", "doublon", "trop lourd", "autre"];

export function validateReview(payload) {
  if (payload?.schema_version !== 1 || !Array.isArray(payload.objects)) throw new Error("Format de tri invalide.");
  const seen = new Set();
  for (const row of payload.objects) {
    if (!row || typeof row.object_id !== "string" || !row.object_id.trim() || seen.has(row.object_id) ||
        !Object.hasOwn(REVIEW_STATUSES, row.status) || typeof row.reason !== "string" || typeof row.note !== "string") {
      throw new Error("Décision invalide ou identifiant en double.");
    }
    seen.add(row.object_id);
  }
  return payload.objects;
}

export class ObjectReview {
  constructor({container, filter, onChange, onOriginal, notify, download, onSaved = () => {}, getContext = () => ({})}) {
    Object.assign(this, {container, filter, onChange, onOriginal, notify, download, onSaved, getContext});
    this.rows = new Map();
    this.audit = new Map();
    this.objects = [];
    try {
      const saved = localStorage.getItem(KEY);
      if (saved) this.rows = new Map(validateReview(JSON.parse(saved)).map(row => [row.object_id, row]));
    } catch { notify("Sauvegarde de tri illisible ; importer un export valide pour la récupérer."); }
    for (const [value, label] of Object.entries(REVIEW_STATUSES)) filter.add(new Option(label, value));
    container.innerHTML = `
      <h2>Tri des objets originaux</h2>
      <p id="reviewSummary" class="muted"></p>
      <strong id="reviewObject"></strong>
      <button type="button" id="reviewOriginal">Afficher l’original</button>
      <label>Décision <select id="reviewStatus"></select></label>
      <label>Motif <select id="reviewReason"></select></label>
      <label>Note de tri <textarea id="reviewNote" rows="2"></textarea></label>
      <button type="button" id="reviewSave">Enregistrer la décision</button>
      <p id="reviewAudit" class="muted"></p>
      <div class="review-actions">
        <button type="button" id="reviewExport">Exporter le tri JSON</button>
        <label>Importer un tri JSON <input id="reviewImport" type="file" accept=".json"></label>
        <label>Importer un audit JSON <input id="auditImport" type="file" accept=".json"></label>
      </div>
      <small>Tri indépendant des notes de sécurité. Sauvegarde dans ce navigateur : exporter pour conserver et partager les décisions. Les imports conservent les décisions locales déjà prises.</small>`;
    this.el = Object.fromEntries([...container.querySelectorAll("[id]")].map(el => [el.id, el]));
    for (const [value, label] of Object.entries(REVIEW_STATUSES)) this.el.reviewStatus.add(new Option(label, value));
    REASONS.forEach(value => this.el.reviewReason.add(new Option(value || "Sans motif", value)));
    this.el.reviewOriginal.onclick = () => onOriginal(this.activeId);
    this.el.reviewSave.onclick = () => this.save();
    this.el.reviewExport.onclick = () => download("nextlife_object_review.json", JSON.stringify(this.payload(), null, 2), "application/json");
    this.el.reviewImport.onchange = event => this.importFile(event, false);
    this.el.auditImport.onchange = event => this.importFile(event, true);
  }
  status(id) { return this.rows.get(id)?.status || "pending"; }
  payload() {
    const rows = new Map(this.rows);
    this.objects.forEach(object => {
      if (!rows.has(object.object_id)) rows.set(object.object_id, {object_id: object.object_id, status: "pending", reason: "", note: "", updated_at: ""});
    });
    return {schema_version: 1, exported_at: new Date().toISOString(), objects: [...rows.values()]};
  }
  persist() { localStorage.setItem(KEY, JSON.stringify(this.payload())); }
  show(id) {
    this.activeId = id;
    const object = this.objects.find(o => o.object_id === id);
    const row = this.rows.get(id);
    this.el.reviewObject.textContent = object ? `${object.imagenet_class} · ${object.object_name} (${id})` : "Choisir un objet";
    for (const key of ["reviewStatus", "reviewReason", "reviewNote", "reviewSave", "reviewOriginal"]) this.el[key].disabled = !object;
    this.el.reviewStatus.value = row?.status || "pending";
    this.el.reviewReason.value = row?.reason || "";
    if (row?.reason && !REASONS.includes(row.reason)) this.el.reviewReason.add(new Option(row.reason, row.reason, true, true));
    this.el.reviewNote.value = row?.note || "";
    const audit = this.audit.get(id);
    this.el.reviewAudit.textContent = audit ? `${audit.faces} faces · ${audit.issues.join(" ; ") || "Aucune alerte automatique ; contrôle visuel nécessaire."}` : "Audit non importé pour cet objet.";
    this.el.reviewSummary.textContent = Object.entries(REVIEW_STATUSES).map(([status, label]) => `${label} : ${this.objects.filter(o => this.status(o.object_id) === status).length}`).join(" · ");
  }
  save() {
    if (!this.activeId) return;
    const status = this.el.reviewStatus.value, reason = this.el.reviewReason.value, note = this.el.reviewNote.value.trim();
    if (["repair", "excluded"].includes(status) && !reason && !note) {
      this.notify("Indiquer un motif ou une note pour une réparation ou une exclusion."); return;
    }
    const previous = this.rows.get(this.activeId);
    const history = previous ? [...(previous.history || []), {...previous, history: undefined}] : [];
    this.rows.set(this.activeId, {object_id: this.activeId, status, reason, note, updated_at: new Date().toISOString(), history, ...this.getContext()});
    try { this.persist(); } catch { this.notify("Sauvegarde navigateur impossible : exporter le tri JSON maintenant."); return; }
    this.onChange();
    this.show(this.activeId);
    this.notify("Décision enregistrée pour l’objet original.");
    this.onSaved();
  }
  async importFile(event, audit) {
    try {
      const file = event.target.files[0];
      if (!file) return;
      const payload = JSON.parse(await file.text());
      if (audit) {
        if (payload.schema_version !== 1 || !Array.isArray(payload.objects) || !payload.objects.every(row => typeof row.object_id === "string" && Array.isArray(row.issues) && row.issues.every(i => typeof i === "string") && Number.isInteger(row.faces))) throw new Error("Format d’audit invalide.");
        this.audit = new Map(payload.objects.map(row => [row.object_id, row]));
        this.onChange();
        this.notify(`Audit importé : ${this.audit.size} objets.`);
      } else {
        const rows = validateReview(payload);
        let added = 0;
        rows.forEach(row => {
          const local = this.rows.get(row.object_id);
          if (!local || (local.status === "pending" && !local.reason && !local.note)) { this.rows.set(row.object_id, row); added++; }
        });
        this.persist();
        this.onChange();
        this.notify(`${added} décisions importées ; ${rows.length - added} décisions locales conservées.`);
      }
      this.show(this.activeId);
    } catch (error) { this.notify(`Import impossible : ${error.message}`); }
    finally { event.target.value = ""; }
  }
}
