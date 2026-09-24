import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { MTLLoader } from "three/addons/loaders/MTLLoader.js";
import { OBJLoader } from "three/addons/loaders/OBJLoader.js";


const DEFAULT_SCENE_METRICS = {
  floorY: -1.25,
  center: new THREE.Vector3(0, 0, 0),
  inspectionPoint: new THREE.Vector3(0, -1.25, 0),
  size: new THREE.Vector3(8, 4.8, 8),
  objectHeightRatio: 0.18,
  objectFootprintRatio: 0.13,
  objectScaleMode: "height",
  objectMinHeight: 0.03,
  objectMaxHeight: 1.2,
  objectMinFootprint: 0.03,
  objectMaxFootprint: 1.2,
  objectLocation: null,
  objectLocationSpace: "world",
  objectPlacementMode: "floorCenter",
  floorOffsetRatio: 0.04,
  cameraYaw: 0,
  cameraDistanceRatio: 0.34,
  cameraMinDistance: 0.25,
  cameraMaxDistance: 4.5,
  cameraHeightRatio: 0.18,
};

// Keep these values aligned with recognition_viewer.js while scene placement is calibrated.
const SCENE_PLACEMENT = {
  art_gallery: {
    objectHeightRatio: 0.05,
    objectFootprintRatio: 0.03,
    objectScaleMode: "height",
    floorOffsetRatio: 0.0953,
    position: { x: 0, y: 0.05, z: 0 },
    cameraYaw: 0.5,
    cameraDistanceRatio: 0.01,
    cameraMinDistance: 0.08,
    cameraMaxDistance: 2.8,
    cameraHeightRatio: 0.2,
  },
  exterior: {
    objectHeightRatio: 0.14,
    objectFootprintRatio: 0.11,
    objectScaleMode: "height",
    sceneLocation: { x: 0, y: 1.31, z: 0 },
    objectLocation: { x: -0.07, y: 0, z: 0 },
    objectLocationSpace: "world",
    objectPlacementMode: "origin",
    floorOffsetRatio: 0,
    position: { x: 0, z: 0 },
    cameraYaw: 0,
    cameraDistanceRatio: 0.26,
    cameraHeightRatio: 0.16,
  },
  exterior_city: {
    objectHeightRatio: 0.15,
    objectFootprintRatio: 0.1,
    objectScaleMode: "height",
    sceneLocation: { x: 0, y: 0.73, z: 0 },
    objectLocation: { x: 0, y: 0.1, z: 0 },
    objectLocationSpace: "world",
    objectPlacementMode: "origin",
    floorOffsetRatio: 0.05,
    position: { x: 0, y: 0, z: 0 },
    cameraYaw: 0,
    cameraDistanceRatio: 0.25,
    cameraHeightRatio: 0.16,
  },
  room_white: {
    objectHeightRatio: 0.15,
    objectFootprintRatio: 0.13,
    objectScaleMode: "height",
    sceneLocation: { x: 0, y: 0.3, z: 0 },
    objectLocation: { x: 0, y: 0.1, z: 0 },
    objectLocationSpace: "world",
    objectPlacementMode: "origin",
    floorOffsetRatio: 0.05,
    position: { x: 0, z: 0 },
    cameraYaw: 0,
    cameraDistanceRatio: 0.15,
    cameraHeightRatio: 0.18,
  },
  sitting_room: {
    objectHeightRatio: 0.15,
    objectFootprintRatio: 0.11,
    objectScaleMode: "height",
    floorOffsetRatio: 0.05,
    position: { x: 0, z: 0.05 },
    cameraYaw: 0,
    cameraDistanceRatio: 0.25,
    cameraHeightRatio: 0.17,
  },
};

const OBJECT_PLACEMENT = {
  labels: {
    "running shoe": { rotationDeg: { x: 0, y: 0, z: 0 } },
  },
  objects: {},
};

function baseDir(path) {
  return path.slice(0, path.lastIndexOf("/") + 1);
}

function assetUrl(path) {
  if (!path) return path;
  if (/^(https?:)?\/\//i.test(path) || path.startsWith("../")) return path;
  return `../${path.replaceAll("\\", "/")}`;
}

function fileName(path) {
  return path.split(/[\\/]/).pop();
}

function vectorFrom(value, fallback = new THREE.Vector3()) {
  if (!value) return fallback.clone();
  if (Array.isArray(value)) return new THREE.Vector3(value[0] || 0, value[1] || 0, value[2] || 0);
  return new THREE.Vector3(value.x || 0, value.y || 0, value.z || 0);
}

function eulerDegFrom(value) {
  const rotation = vectorFrom(value);
  return new THREE.Euler(
    THREE.MathUtils.degToRad(rotation.x),
    THREE.MathUtils.degToRad(rotation.y),
    THREE.MathUtils.degToRad(rotation.z),
    "XYZ"
  );
}

function objectPlacementFor(trial) {
  return {
    ...(OBJECT_PLACEMENT.labels[trial?.imagenet_class] || {}),
    ...(OBJECT_PLACEMENT.objects[trial?.object_id] || {}),
  };
}

export class ModelViewer {
  constructor(canvas) {
    this.canvas = canvas;
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: false });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.setClearColor(0xe4ebe8, 1);
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.05;

    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(38, 1, 0.01, 1000);
    this.camera.position.set(0, 0.2, 4.2);
    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.08;
    this.controls.enablePan = false;

    this.group = new THREE.Group();
    this.sceneGroup = new THREE.Group();
    this.currentSceneId = "";
    this.sceneMetrics = { ...DEFAULT_SCENE_METRICS };
    this.gltfLoader = new GLTFLoader();
    this.activeSceneRoot = null;
    this.scene.add(this.sceneGroup, this.group);

    const hemi = new THREE.HemisphereLight(0xffffff, 0x4d5754, 2.3);
    const key = new THREE.DirectionalLight(0xffffff, 2.1);
    key.position.set(4, 6, 5);
    const fill = new THREE.DirectionalLight(0xffffff, 0.9);
    fill.position.set(-5, 3, -4);
    this.scene.add(hemi, key, fill);

    this.setNeutralScene();
    this.resize();
    window.addEventListener("resize", () => this.resize());
    this.resizeObserver = new ResizeObserver(() => this.resize());
    this.resizeObserver.observe(this.canvas);
  }

  resize() {
    const rect = this.canvas.getBoundingClientRect();
    if (rect.width <= 0 || rect.height <= 0) return;
    this.renderer.setSize(Math.max(1, Math.round(rect.width)), Math.max(1, Math.round(rect.height)), false);
    this.camera.aspect = rect.width / rect.height;
    this.camera.updateProjectionMatrix();
    if (this.inspectionBounds && this.currentSceneId === "__neutral__") this.frameInspection();
  }

  disposeObject(object) {
    object.traverse((node) => {
      if (node.geometry) node.geometry.dispose();
      if (!node.material) return;
      const materials = Array.isArray(node.material) ? node.material : [node.material];
      materials.forEach((material) => {
        Object.values(material).forEach((value) => {
          if (value?.isTexture) value.dispose();
        });
        material.dispose();
      });
    });
  }

  clear() {
    while (this.group.children.length) this.disposeObject(this.group.children.pop());
  }

  clearSceneGroup() {
    while (this.sceneGroup.children.length) this.disposeObject(this.sceneGroup.children.pop());
    this.activeSceneRoot = null;
  }

  async load(spec, trial = null) {
    this.inspectionBounds = null;
    this.clear();
    if (!spec?.obj) throw new Error("Modèle sans OBJ");
    const manager = new THREE.LoadingManager();
    // Local repairs keep their filenames: request a fresh OBJ, MTL and textures
    // for every inspection, including the "Original" and "Recentrer" actions.
    const revision = crypto.randomUUID();
    manager.setURLModifier((url) => {
      if (/^(data:|blob:)/i.test(url)) return url;
      const asset = new URL(url, document.baseURI);
      if (spec.texture_folder && /\.(png|jpg|jpeg|bmp|webp|tif|tiff)$/i.test(asset.pathname)) {
        const textureRoot = new URL(`${assetUrl(spec.texture_folder)}/`, document.baseURI);
        asset.href = new URL(fileName(asset.pathname), textureRoot).href;
      }
      asset.searchParams.set("inspection_revision", revision);
      return asset.href;
    });

    const objLoader = new OBJLoader(manager);
    if (spec.mtl) {
      const mtlLoader = new MTLLoader(manager);
      mtlLoader.setPath(assetUrl(baseDir(spec.mtl)));
      mtlLoader.setResourcePath(assetUrl(baseDir(spec.mtl)));
      const materials = await mtlLoader.loadAsync(fileName(spec.mtl));
      materials.preload();
      this.normalizeMaterials(materials);
      objLoader.setMaterials(materials);
    }
    objLoader.setPath(assetUrl(baseDir(spec.obj)));
    const object = await objLoader.loadAsync(fileName(spec.obj));
    this.fit(object, objectPlacementFor(trial));
    this.group.add(object);
  }

  async setScene(sceneId, useContext = true) {
    if (!useContext) {
      if (this.currentSceneId !== "__neutral__") this.setNeutralScene();
      return true;
    }
    const nextSceneId = sceneId || "room_white";
    if (nextSceneId === this.currentSceneId) return true;
    this.currentSceneId = nextSceneId;
    this.clearSceneGroup();
    try {
      await this.loadGltfScene(nextSceneId);
      return true;
    } catch {
      this.makeProceduralScene(nextSceneId);
      return false;
    }
  }

  async loadGltfScene(sceneId) {
    const placement = SCENE_PLACEMENT[sceneId] || {};
    const gltf = await this.gltfLoader.loadAsync(assetUrl(`Scenes/${sceneId}/scene.gltf`));
    const root = gltf.scene;
    root.traverse((node) => {
      if (!node.isMesh) return;
      node.frustumCulled = false;
      const materials = Array.isArray(node.material) ? node.material : [node.material];
      materials.forEach((material) => {
        if (!material) return;
        material.side = THREE.DoubleSide;
        material.depthWrite = true;
      });
    });

    const box = new THREE.Box3().setFromObject(root);
    const size = new THREE.Vector3();
    const center = new THREE.Vector3();
    box.getSize(size);
    box.getCenter(center);
    const scale = 5.8 / (Math.max(size.x, size.y, size.z) || 1);
    root.scale.setScalar(scale);
    root.position.set(-center.x * scale, -box.min.y * scale - 1.35, -center.z * scale);
    if (placement.sceneLocation) root.position.add(vectorFrom(placement.sceneLocation));
    root.rotation.set(0, 0, 0);
    root.updateMatrixWorld(true);
    this.scene.background = new THREE.Color(0xe6ece9);
    this.renderer.setClearColor(0xe6ece9, 1);
    this.sceneGroup.add(root);
    this.activeSceneRoot = root;
    this.updateSceneMetrics(sceneId);
  }

  scenePointToWorld(point) {
    const local = vectorFrom(point);
    return this.activeSceneRoot ? this.activeSceneRoot.localToWorld(local) : local;
  }

  updateSceneMetrics(sceneId) {
    const box = new THREE.Box3().setFromObject(this.sceneGroup);
    const size = new THREE.Vector3();
    const center = new THREE.Vector3();
    box.getSize(size);
    box.getCenter(center);
    const placement = SCENE_PLACEMENT[sceneId] || {};
    const floorOffsetRatio = placement.floorOffsetRatio ?? DEFAULT_SCENE_METRICS.floorOffsetRatio;
    const floorY = Number.isFinite(box.min.y)
      ? box.min.y + Math.max(size.y, 1) * floorOffsetRatio
      : DEFAULT_SCENE_METRICS.floorY;
    const position = placement.position || {};
    this.sceneMetrics = {
      bounds: box,
      floorY,
      center,
      inspectionPoint: new THREE.Vector3(
        center.x + (position.x || 0) * size.x,
        floorY + (position.y || 0) * size.y,
        center.z + (position.z || 0) * size.z
      ),
      size,
      objectHeightRatio: placement.objectHeightRatio || DEFAULT_SCENE_METRICS.objectHeightRatio,
      objectFootprintRatio: placement.objectFootprintRatio || DEFAULT_SCENE_METRICS.objectFootprintRatio,
      objectScaleMode: placement.objectScaleMode || DEFAULT_SCENE_METRICS.objectScaleMode,
      objectMinHeight: placement.objectMinHeight || DEFAULT_SCENE_METRICS.objectMinHeight,
      objectMaxHeight: placement.objectMaxHeight || DEFAULT_SCENE_METRICS.objectMaxHeight,
      objectMinFootprint: placement.objectMinFootprint || DEFAULT_SCENE_METRICS.objectMinFootprint,
      objectMaxFootprint: placement.objectMaxFootprint || DEFAULT_SCENE_METRICS.objectMaxFootprint,
      objectLocation: placement.objectLocation || DEFAULT_SCENE_METRICS.objectLocation,
      objectLocationSpace: placement.objectLocationSpace || DEFAULT_SCENE_METRICS.objectLocationSpace,
      objectPlacementMode: placement.objectPlacementMode || DEFAULT_SCENE_METRICS.objectPlacementMode,
      cameraYaw: placement.cameraYaw ?? DEFAULT_SCENE_METRICS.cameraYaw,
      cameraDistanceRatio: placement.cameraDistanceRatio ?? DEFAULT_SCENE_METRICS.cameraDistanceRatio,
      cameraMinDistance: placement.cameraMinDistance ?? DEFAULT_SCENE_METRICS.cameraMinDistance,
      cameraMaxDistance: placement.cameraMaxDistance ?? DEFAULT_SCENE_METRICS.cameraMaxDistance,
      cameraHeightRatio: placement.cameraHeightRatio ?? DEFAULT_SCENE_METRICS.cameraHeightRatio,
    };
  }

  setNeutralScene() {
    this.currentSceneId = "__neutral__";
    this.clearSceneGroup();
    this.scene.background = new THREE.Color(0xe7ece9);
    this.renderer.setClearColor(0xe7ece9, 1);
    const floor = new THREE.Mesh(
      new THREE.PlaneGeometry(6, 6),
      new THREE.MeshStandardMaterial({ color: 0xd5dcd8, roughness: 0.94, metalness: 0 })
    );
    floor.rotation.x = -Math.PI / 2;
    floor.position.y = -1;
    this.sceneGroup.add(floor);
    this.sceneMetrics = {
      ...DEFAULT_SCENE_METRICS,
      floorY: -1,
      center: new THREE.Vector3(0, 0, 0),
      inspectionPoint: new THREE.Vector3(0, -1, 0),
      size: new THREE.Vector3(6, 3, 6),
      objectHeightRatio: 0.4,
      objectFootprintRatio: 0.28,
      cameraDistanceRatio: 0.48,
      cameraMinDistance: 1.5,
      cameraMaxDistance: 4.5,
      cameraHeightRatio: 0.1,
    };
  }

  makeProceduralScene(sceneId) {
    this.clearSceneGroup();
    const colors = {
      art_gallery: [0xf0eee8, 0xd8d2c6, 0xf7f4ec],
      exterior: [0xdceaf2, 0x8f9f78, 0xc8d8df],
      exterior_city: [0xd9e2e7, 0x9aa0a0, 0xc8ced2],
      room_white: [0xe9eeeb, 0xd9dfdc, 0xf6f7f5],
      sitting_room: [0xeadfd4, 0xb49a7e, 0xead8c7],
    };
    const [bg, floorColor, wallColor] = colors[sceneId] || colors.room_white;
    this.scene.background = new THREE.Color(bg);
    this.renderer.setClearColor(bg, 1);
    const floor = new THREE.Mesh(
      new THREE.PlaneGeometry(8, 8),
      new THREE.MeshStandardMaterial({ color: floorColor, roughness: 0.92 })
    );
    floor.rotation.x = -Math.PI / 2;
    floor.position.y = -1.25;
    const wall = new THREE.Mesh(
      new THREE.PlaneGeometry(8, 4.8),
      new THREE.MeshStandardMaterial({ color: wallColor, roughness: 0.96 })
    );
    wall.position.set(0, 1.15, -2.55);
    this.sceneGroup.add(floor, wall);
    this.updateSceneMetrics(sceneId);
  }

  fit(object, objectPlacement = {}) {
    object.traverse((node) => {
      if (!node.isMesh) return;
      node.frustumCulled = false;
      const materials = Array.isArray(node.material) ? node.material : [node.material];
      materials.forEach((material) => {
        if (material) material.side = THREE.DoubleSide;
      });
    });

    const box = this.robustBox(object) || new THREE.Box3().setFromObject(object);
    const size = new THREE.Vector3();
    const center = new THREE.Vector3();
    box.getSize(size);
    box.getCenter(center);
    const sceneSize = this.sceneMetrics.size || DEFAULT_SCENE_METRICS.size;
    const sceneHeight = Math.max(sceneSize.y || 0, 1);
    const sceneFootprint = Math.max(Math.min(sceneSize.x || 0, sceneSize.z || 0), 1);
    const targetHeight = THREE.MathUtils.clamp(
      sceneHeight * this.sceneMetrics.objectHeightRatio,
      this.sceneMetrics.objectMinHeight,
      this.sceneMetrics.objectMaxHeight
    );
    const targetFootprint = THREE.MathUtils.clamp(
      sceneFootprint * this.sceneMetrics.objectFootprintRatio,
      this.sceneMetrics.objectMinFootprint,
      this.sceneMetrics.objectMaxFootprint
    );
    const heightScale = targetHeight / Math.max(size.y, 0.0001);
    const footprintScale = targetFootprint / Math.max(size.x, size.z, 0.0001);
    const scale = this.sceneMetrics.objectScaleMode === "fit" ? Math.min(heightScale, footprintScale) : heightScale;
    const floorY = this.sceneMetrics.floorY ?? DEFAULT_SCENE_METRICS.floorY;
    let inspectionPoint = this.sceneMetrics.inspectionPoint || DEFAULT_SCENE_METRICS.inspectionPoint;
    if (this.sceneMetrics.objectLocation) {
      inspectionPoint = this.sceneMetrics.objectLocationSpace === "scene"
        ? this.scenePointToWorld(this.sceneMetrics.objectLocation)
        : vectorFrom(this.sceneMetrics.objectLocation);
    }

    object.scale.setScalar(scale);
    if (this.sceneMetrics.objectPlacementMode === "origin") {
      object.position.copy(inspectionPoint);
    } else {
      object.position.set(
        inspectionPoint.x - center.x * scale,
        floorY - box.min.y * scale + 0.015,
        inspectionPoint.z - center.z * scale
      );
    }
    object.rotation.copy(eulerDegFrom(objectPlacement.rotationDeg));
    object.updateMatrixWorld(true);

    // Quantile bounds are useful for framing, but can omit feet and thin supports.
    // Ground the complete geometry after scale and rotation; preserve explicit
    // scene-origin placements, whose coordinates are calibrated separately.
    if (this.sceneMetrics.objectPlacementMode !== "origin") {
      const groundBox = new THREE.Box3().setFromObject(object, true);
      if (!groundBox.isEmpty() && Number.isFinite(groundBox.min.y)) {
        object.position.y += floorY + 0.015 - groundBox.min.y;
        object.updateMatrixWorld(true);
      }
    }

    const fittedBox = new THREE.Box3().setFromObject(object);
    const fittedCenter = new THREE.Vector3();
    const fittedSize = new THREE.Vector3();
    fittedBox.getCenter(fittedCenter);
    fittedBox.getSize(fittedSize);
    const objectViewHeight = Math.max(fittedSize.y, targetHeight, 0.0001);
    const cameraDistance = THREE.MathUtils.clamp(
      sceneFootprint * this.sceneMetrics.cameraDistanceRatio,
      this.sceneMetrics.cameraMinDistance,
      this.sceneMetrics.cameraMaxDistance
    );
    const yaw = this.sceneMetrics.cameraYaw || 0;
    const target = new THREE.Vector3(
      fittedCenter.x,
      fittedBox.min.y + objectViewHeight * 0.52,
      fittedCenter.z
    );
    this.camera.position.set(
      target.x + Math.sin(yaw) * cameraDistance,
      target.y + Math.max(sceneHeight * this.sceneMetrics.cameraHeightRatio, objectViewHeight * 0.35),
      target.z + Math.cos(yaw) * cameraDistance
    );
    this.controls.target.copy(target);
    this.camera.lookAt(target);
    this.controls.minDistance = Math.max(0.25, objectViewHeight * 0.35);
    this.controls.maxDistance = Math.max(3.5, cameraDistance * 2.2);
    this.controls.update();
    if (this.currentSceneId === "__neutral__") {
      this.inspectionBounds = new THREE.Box3().setFromObject(object, true);
      this.frameInspection();
    }
  }

  frameInspection() {
    const box = this.inspectionBounds;
    if (!box || box.isEmpty()) return;
    const center = box.getCenter(new THREE.Vector3());
    const radius = box.getSize(new THREE.Vector3()).length() / 2;
    if (!Number.isFinite(radius) || radius <= 0) return;
    const direction = this.camera.position.clone().sub(this.controls.target).normalize();
    if (!direction.lengthSq()) direction.set(0, 0.35, 1).normalize();
    this.camera.position.copy(center).add(direction);
    this.camera.lookAt(center);
    this.camera.updateMatrixWorld(true);
    const inverseRotation = this.camera.quaternion.clone().invert();
    const tanY = Math.tan(THREE.MathUtils.degToRad(this.camera.getEffectiveFOV()) / 2);
    const tanX = tanY * this.camera.aspect;
    let distance = 0;
    // Fit all eight corners, including depth, with a 12% screen-space margin.
    for (const x of [box.min.x, box.max.x]) {
      for (const y of [box.min.y, box.max.y]) {
        for (const z of [box.min.z, box.max.z]) {
          const p = new THREE.Vector3(x, y, z).sub(center).applyQuaternion(inverseRotation);
          distance = Math.max(distance, p.z + 1.12 * Math.abs(p.x) / tanX,
            p.z + 1.12 * Math.abs(p.y) / tanY, p.z + radius * 0.05);
        }
      }
    }
    this.controls.target.copy(center);
    this.controls.minDistance = radius * 0.05;
    this.controls.maxDistance = Math.max(distance * 5, radius * 10);
    this.camera.near = Math.max(radius * 0.001, 0.000001);
    this.camera.far = Math.max(1000, this.controls.maxDistance + radius * 4);
    this.camera.position.copy(center).addScaledVector(direction, distance);
    this.camera.updateProjectionMatrix();
    this.controls.update();
  }

  normalizeMaterials(materials) {
    Object.values(materials.materials || {}).forEach((material) => {
      if (!material) return;
      if (material.emissive) material.emissive.setRGB(0, 0, 0);
      if (material.map && material.color) material.color.setRGB(1, 1, 1);
      material.needsUpdate = true;
    });
  }

  robustBox(object) {
    const axes = [[], [], []];
    object.updateMatrixWorld(true);
    object.traverse((node) => {
      if (!node.isMesh || !node.geometry?.attributes?.position) return;
      const position = node.geometry.attributes.position;
      const point = new THREE.Vector3();
      const step = Math.max(1, Math.floor(position.count / 30000));
      for (let index = 0; index < position.count; index += step) {
        point.fromBufferAttribute(position, index).applyMatrix4(node.matrixWorld);
        if (Number.isFinite(point.x) && Number.isFinite(point.y) && Number.isFinite(point.z)) {
          axes[0].push(point.x);
          axes[1].push(point.y);
          axes[2].push(point.z);
        }
      }
    });
    if (axes[0].length < 8) return null;
    const pick = (values, quantile) => {
      values.sort((a, b) => a - b);
      return values[Math.floor((values.length - 1) * quantile)];
    };
    const min = new THREE.Vector3(pick(axes[0], 0.02), pick(axes[1], 0.02), pick(axes[2], 0.02));
    const max = new THREE.Vector3(pick(axes[0], 0.98), pick(axes[1], 0.98), pick(axes[2], 0.98));
    if (max.x <= min.x || max.y <= min.y || max.z <= min.z) return null;
    return new THREE.Box3(min, max);
  }

  render() {
    this.controls.update();
    this.renderer.render(this.scene, this.camera);
  }
}
