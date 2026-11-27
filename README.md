# The Grading Simulator

Welcome to **The Grading Simulator** repository. Follow the setup instructions below carefully to ensure all dependencies, Git LFS assets, and external packages load correctly.

---

## 🛠️ Prerequisites

Before cloning or opening the project, ensure you have the following installed:

1. **Git & Git LFS (Required):**
   - Download and install [Git](https://git-scm.com/downloads) and [Git LFS](https://git-lfs.com/).
2. **Unreal Engine 4.27.2** (Installed via Epic Games Launcher).
3. **Visual Studio 2022** (or 2019) *(Required if compiling custom C++ code)*:
   - Ensure the **"Game development with C++"** workload is checked.
   - Ensure the **"Unreal Engine installer"** component is selected in the Visual Studio Installer.
4. **.NET Framework** (usually installed automatically with Visual Studio).

---

## 📥 Cloning the Repository (Git LFS Required)

> **⚠️ DO NOT use GitHub's "Download ZIP" button.**  
> This repository uses **Git LFS (Large File Storage)** to manage Unreal Engine binary files (`.umap`, `.uasset`, etc.). Downloading the repository as a ZIP archive will only download lightweight text pointer files instead of the actual game assets, causing the project to fail on load.

1. Open your terminal (Git Bash, PowerShell, or Command Prompt) and initialize Git LFS (only needed once per machine):
   ```bash
   git lfs install
   ```
2. Clone the repository using Git:
   ```bash
   git clone https://github.com/tomSum3345678/The-Grading-Simulator.git
   ```
3. *(Troubleshooting)* If you already cloned the repository **before** installing Git LFS, navigate into the project folder and pull the LFS files manually:
   ```bash
   cd The-Grading-Simulator
   git lfs install
   git lfs pull
   ```

---

## ⚠️ CRITICAL: Required Project Assets

Due to **copyright and third-party licensing restrictions** (as well as repository size limits), all third-party assets are excluded from this Git repository and cannot be redistributed here. You **must** download and install them into the exact directories below **before** opening any levels in the Unreal Editor, or references within the map and Blueprints will break permanently.

### Step 1: Add Fab Library Packages (via Epic Games Launcher)

Fab Unreal Engine packages are stored as manifest caches in your local Fab Library and **cannot** be copied directly via File Explorer. Claim both packages on Fab first, then add them through **Epic Games Launcher > Unreal Engine > Library > Fab Library**:

1. **Environment Pack (`Content/Office_Pack_Vol_1/`)**
   - **Source:** [Office Pack Vol. 1 (Fab)](https://www.fab.com/listings/ce136033-3265-46d3-ac4d-fdbd5c9d0462)
   - **How to add:**
     - In Epic Games Launcher (**Fab Library**), locate *Office Pack Vol. 1*, click **Add to Project**, and select `The-Grading-Simulator`.
     - It will be installed directly to `Content/Office_Pack_Vol_1/` (no further moving required).

2. **Letter Model (`Content/ExternalAssets/ArchVisPolishPack02/`)**
   - **Source:** [Letter Model (Fab)](https://www.fab.com/listings/3f15cf5f-0ea0-45d9-896b-ec3c5bf6cb9d)
   - **How to add:**
     1. In Epic Games Launcher (**Fab Library**), locate *ArchVis Polish Pack 02*, click **Add to Project**, and select `The-Grading-Simulator` (this initially places it at `Content/ArchVisPolishPack02/`).
     2. Launch the project in **Unreal Engine 4.27.2** (do **not** open `MainMenu_Level.umap` yet).
     3. In the **Content Browser**, create the folder `Content/ExternalAssets/` (if it does not exist), then drag `ArchVisPolishPack02` into `Content/ExternalAssets/` and select **Move Here**.
     4. Right-click `Content/` in the Content Browser, click **Fix Up Redirectors in Folder**, and press `Ctrl + Shift + S` to save all.

### Step 2: Manually Create Subfolders & Import Raw 3D Models / Audio

The remaining assets are distributed as raw source archives (containing `source/` and `textures/` folders with `.fbx`, `.obj`, `.png`, or `.wav` files) rather than `.uasset` packages. **Do not** copy the raw `source/` or `textures/` folders directly into `Content/` via File Explorer—they must be imported through the Unreal Editor:

| Asset | Source Link | Target Folder in Content Browser |
| :--- | :--- | :--- |
| **Car Model** | [Fab Listing](https://www.fab.com/listings/ccfbacb3-49dd-4719-b5ce-457753739068) | `Content/ExternalAssets/Car_Assets/` |
| **Character Model** | [Fab Listing](https://www.fab.com/listings/72606a4a-f409-478d-8ddd-b76014fcaccc) | `Content/ExternalAssets/Character_Assets/` |
| **Paper Model** | [Fab Listing](https://www.fab.com/listings/1b409861-bcdc-4f60-9ce4-3e0d3955d5e2) | `Content/ExternalAssets/Paper_Model/` |
| **Mouse Click SFX** | [YouTube Reference](https://www.youtube.com/watch?v=UBKNrfUgYqg&list=PLhDz2jO-FE_eHcmuZzxKC_S63fudnct3p&index=3) / [CreatorAssets](https://creatorassets.com) | `Content/ExternalAssets/` *(or designated audio folder)* |

**How to import them:**
1. Locate the downloaded/extracted files (if downloaded via Epic Games Launcher's Fab Library, check your local `VaultCache\FabLibrary\` directory; otherwise extract the downloaded `.zip` archive).
2. In the UE4 **Content Browser**, manually create the following subfolders under `Content/ExternalAssets/`:
   - `Car_Assets`
   - `Character_Assets`
   - `Paper_Model`
3. Open each target subfolder in the **Content Browser**, click **Import** (or drag and drop the source file in), and press `Ctrl + Shift + S` to save all generated `.uasset` files:
   - **Car Model (`Car_Assets/`):** Open the extracted `source/` folder and import **`coupe-sport-generic-mmc.fbx`** (ignore the `coupe-sport-generic-mmc.fbm` cache folder). Keep **Import Materials** and **Import Textures** enabled in the FBX Import Options; if any textures do not auto-assign, import the `.png` files from the `textures/` folder into `Car_Assets/` as well.
   - **Character Model (`Character_Assets/`) & Paper Model (`Paper_Model/`):** Import their respective `.fbx`/`.obj` mesh files (and textures, if not embedded) into their matching subfolders.

### Expected Folder Structure

Before opening `MainMenu_Level.umap`, verify your `Content/` directory matches this exact layout:

```text
Content/
├── FirstPersonBP/
├── Office_Pack_Vol_1/              # Added via Epic Games Launcher (Fab Library)
└── ExternalAssets/                 # Create manually
    ├── ArchVisPolishPack02/        # Added via Fab Library, then moved here in Content Browser
    ├── Car_Assets/                 # Create manually & import coupe-sport-generic-mmc.fbx
    ├── Character_Assets/           # Create manually & import model via Content Browser
    └── Paper_Model/                # Create manually & import model via Content Browser
```

---

## 🎮 Getting Started & Map Location

Once the repository is cloned via Git LFS and all external assets are placed/imported into the `Content/` directory, you can safely launch the project.

- **Main Menu Level Path:**
  ```text
  /Content/FirstPersonBP/MainMenu/MainMenu_Level.umap
  ```
  *Open this level first to run or test the game flow.*

---

## 📦 Third-Party Asset Attribution

Below are the credits and source links for the external assets and 3D models integrated into this project:

### 3D Models & Environment Packs (Fab)
- **Office Environment Pack:** [Fab Listing](https://www.fab.com/listings/ce136033-3265-46d3-ac4d-fdbd5c9d0462)
- **Character Model:** [Fab Listing](https://www.fab.com/listings/72606a4a-f409-478d-8ddd-b76014fcaccc)
- **Letter Model:** [Fab Listing](https://www.fab.com/listings/3f15cf5f-0ea0-45d9-896b-ec3c5bf6cb9d)
- **Car Model:** [Fab Listing](https://www.fab.com/listings/ccfbacb3-49dd-4719-b5ce-457753739068)
- **Paper Model:** [Fab Listing](https://www.fab.com/listings/1b409861-bcdc-4f60-9ce4-3e0d3955d5e2)

### Audio & Sound Effects
- **Mouse Click SFX:** [YouTube Reference](https://www.youtube.com/watch?v=UBKNrfUgYqg&list=PLhDz2jO-FE_eHcmuZzxKC_S63fudnct3p&index=3) (Official download via [CreatorAssets](https://creatorassets.com))
