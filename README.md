# C-Go GTK WebKit Bridge

A minimal, vibe-coded C-Go bridge for GTK WebKit with a very small surface area.

                      ::==::                   ⠀⠀⠀
                  :=++++++++=:.                 
              ::++++++++++++++++-:             
          .:=++++++++++++++++++++++=:.        
       :-++++++++=-..      ..--++++++++-: 
   .:+++++++++-.                .-+++++++++:.  ⠀  ⢀⣠⣤⣤⣤⣤⣄⡀⠀⠀⠀⠀⠀⠀⠀⣠⣤⠶⠚⠛⠛⠶⢦⣄
   +++++++++-                      :++++++***.  ⢠⡾⠋⠁⠀⠀⠀⠀⠈⠙⢷⣀⠀⠀⠀⣠⡾⠋⠀⠀⠀⠀⠀⠀⠀⠙⢷⡀ 
   +++++++=                          =+******- ⢠⡟⠀⣀⡀⠀⠀⠀⠀⠀⠀⠀⢿⡇⠀⢰⡿⠁⣠⣤⡀⠀⠀⠀⠀⠀⠀⠘⣧ ⠀⠀⠀⠀⠀⠀⠀
   ++++++=         .--=++==-.      :=********- ⣿⠁⣾⣿⡿⣆⠀⠀⠀⠀⠀⠀⢸⣷⠀⢸⡇⢸⣿⡿⢿⠀⠀⠀⠀⠀⠀⠀⣿   ⠀⠀⠀⠀⠀⠀
   ++++++        .=++++++++++=..-+***********- ⢹⡆⠻⢿⡷⠃⠀⠀⠀⠀⠀⠀⣸⡇⠀⠸⣿⡈⠛⠟⠋⠀⠀⠀⠀⠀⠀⣰⡏    ⠀⠀⠀⠀⠀⠀⠀
   +++++:       .+++++++++++*****************-  ⠻⣄⠀⠀⠀⠀⠀⠀⠀⢀⣴⢟⣁⣦⣤⣿⣿⣦⣀⠀⠀⠀⠀⢀⣠⡴⠏    ⠀⠀⠀⠀⠀⠀⠀
   +++++.       -++++++++********************-   ⠈⠛⢶⣤⣤⣤⣶⠞⣿⣷⣿⣿⣿⣿⣿⣿⣿⣟⠻⠿⠿⠟⠛⠉
   +++++.       -++++*#####******************-          ⢰⣿⠋⠀⠉⠉⠉⠉⠀⠈⠻⣷
   +++++:       .+*###########***************-          ⠈⢿⣦⣤⣴⢶⡶⢶⣤⣄⣴⡿
   ++++++        .+##########+. :=***********-            ⢹⡏⠀⢸⡇⠀⢻⠉⠉ 
   ++++++=         .-++***+-.      .-+*******-            ⠘⢿⣤⣾⢷⣤⣾⠄
   ++++*##+.                         +###****-
   **#######=                      -#########.
   .-*########=:                .=########*-:
       :-########+=:..    ..:=+########=-
          .-*######################*-.
              :-################=:
                  -+########*-.
                     :-

> **⚠️ Early Prototype / Proof of Concept**
> 
> This was a quick vibe-coded experiment. It currently only works on my machine, contains hard-coded paths, memory leaks, and other issues. 
> It is **not production-ready**. I'm keeping it public as a learning project and to get feedback from the community.

## Overview

This bridge lets you:
- Spawn and manage multiple WebKit windows from Go
- Call Go functions from JavaScript with C acting as a thin courier
- Pass JSON-style data cleanly between Go and the frontend

**Only ~382 lines of C** — everything else stays in Go.

**Current Status**: Linux (GTK WebKit + X11) — fully working  
iOS / Android support is planned (WIP).

## Features
- Multi-window support
- Simple async call pattern (`CallCGo()`)
- Low overhead JSON bridging
- Clean separation of concerns

## Quick Start

### Ground Running Guide

#### 1. Initial Setup

1. Open `main.go`
2. Set your main window name:
   - Line 61 and 77
3. Set your page origin name on **line 63**
4. Set your default window size on **lines 79 & 80** (width and height)

#### 2. Adding a New Go Function

1. Open `handlers.go`
2. Add the function to the whitelist (`funcWhitelist` on **line 30**):
   - Function name
   - Handler name
   - Number of arguments
   - Argument types
3. Add the actual function handler starting at **line 113**

#### 3. How Calling Go from JavaScript Works

The bridge uses a simple pattern where **C acts as the courier** between JavaScript and Go.

- `CallCGo()` sends the request from JS → C → Go
- The response flows back: Go → C → JavaScript (via `ReceiveFromGo`)

#### 4. Basic Example: Calling Go from JS

```javascript
async function FirstCall() {
    const terminal = document.getElementById("htmlTerminalHTMLID");

    let ReturnValue = await CallCGo(
        "SayHello",           // Function name in Go
        "AdrLane1",           // Arbitrary unique address
        "Main Window",        // Target window name
        ["Juan"],             // Arguments
        ["string"]            // Argument types
    );

    terminal.value += "Go says: " + ReturnValue + "\n";
}
```

**Parameter breakdown:**
- `"SayHello"` → Name of the Go function
- `"AdrLane1"` → Arbitrary unique call identifier
- `"Main Window"` → Name of the window that should receive the response
- `["Juan"]` → Array of arguments
- `["string"]` → Array of corresponding argument types

#### 5. Opening a New Window

```javascript
async function OpenNewWindow() {
    const terminal = document.getElementById("htmlTerminalHTMLID");

    let response = await CallCGo(
        "OpenNewWindow",
        "Adr102TryNewWindowLane",
        "Main Window",
        ["AnotherWindow", "HTMLAddress", "file:///path/to/page.html", 800, 600],
        ["string", "string", "string", "int", "int"]
    );

    let success = (response === "true");
    terminal.value += "New window opened: " + success + "\n";
}
```

**Parameter notes:**
- `"HTMLAddress"` = load from file path / URL
- `"HTMLString"` = load from base64-encoded HTML string

#### 6. Sending Data / Commands to Another Window

```javascript
async function SendToOtherWindow() {
    const terminal = document.getElementById("htmlTerminalHTMLID");

    let ReturnValue = await CallCGo(
        "StringSend",                                      // Function name
        "AdrLane3",                                        // Arbitrary address
        "Another Window",                                  // Target window name
        ["eval(document.getElementById('SpanHTMLID').innerHTML = 'test past!')"],
        ["string"]
    );

    terminal.value += "Result: " + ReturnValue + "\n";
}
```
#### 7. After compiling

Check over all commands with the exception of "pass the string."

After opening a window from a string, then use "pass the string" to see the main window pass a command to a child window.
 ---

### Additional Tips:
- Keep argument types accurate (`string`, `int`, `bool`, etc.).
- Every call needs a unique address (you can reuse them across calls if you want, but unique is safer).
- All communication is done via JSON under the hood.
- Once you are finished with your html and handlers, use BridgeDev.sh to allow use of the web console and BridgeBuild.sh to not allow it.



## How It Works

C only handles the absolute minimum:
- Window creation/spawning
- Message passing between Go and WebKit

Everything else is handled in Go.

## License

This project is licensed under the [MIT License](LICENSE) — feel free to use, modify, and contribute.


