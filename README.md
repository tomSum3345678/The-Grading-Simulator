# The Grading Simulator

Welcome to **The Grading Simulator** repository. Follow the setup instructions below carefully to ensure all dependencies and assets load correctly.

---

## 🛠️ Prerequisites

Because this project contains custom C++ code, you **may have to** install the following before attempting to build or open the project:

1.  **Unreal Engine 4.27.2** (Installed via Epic Games Launcher).
2.  **Visual Studio 2022** (or 2019).
    *   During installation, ensure the **"Game development with C++"** workload is checked.
    *   Ensure the **"Unreal Engine installer"** component is selected in the Visual Studio Installer.
3.  **.NET Framework** (usually installed automatically with Visual Studio).

---

## ⚠️ CRITICAL: Required Project Assets

This project requires external assets hosted on Fab. You **must** download and install this package before opening any levels in the Unreal Editor, or references within the map will break permanently.

1. Download the required asset package from this [Fab Listing](https://www.fab.com/listings/ce136033-3265-46d3-ac4d-fdbd5c9d0462).
2. Extract and place the downloaded folder directly into your project's directory structure under:
   ```text
   Content/
   ```

---

## 🎮 Getting Started & Map Location

Once the dependencies are placed in the `Content/` directory, you can safely launch the project.

* **Main Menu Level Path:** 
  ```text
  /Content/FirstPersonBP/MainMenu/MainMenu_Level.umap
  ```
  *Open this level first to run or test the game flow.*

---

## 📦 Third-Party Asset Attribution

Below are the credits and source links for the external assets and 3D models integrated into this project:

### 3D Models & Characters (Fab)
* **Character Model:** [Fab Listing](https://www.fab.com/listings/72606a4a-f409-478d-8ddd-b76014fcaccc)
* **Letter Model:** [Fab Listing](https://www.fab.com/listings/3f15cf5f-0ea0-45d9-896b-ec3c5bf6cb9d)
* **Car Model:** [Fab Listing](https://www.fab.com/listings/ccfbacb3-49dd-4719-b5ce-457753739068)
* **Paper Model:** [Fab Listing](https://www.fab.com/listings/1b409861-bcdc-4f60-9ce4-3e0d3955d5e2)

### Audio & Sound Effects
* **Mouse Click SFX:** [YouTube Reference](https://www.youtube.com/watch?v=UBKNrfUgYqg&list=PLhDz2jO-FE_eHcmuZzxKC_S63fudnct3p&index=3) (Official download via [CreatorAssets](https://creatorassets.com))
