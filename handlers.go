package main

/*
#include <stdlib.h>
#include <stdbool.h>
// 1. Tell Go where the standard free function is
// 2. Declare your specific C functions so Go knows their "shape"
extern void* OpenPhysicalWindow(char* name, char* type, char* source, int w, int h, char* baseURI);
extern bool ClosePhysicalWindow(char* name);
extern void OpenNativeFilePicker(void* web_view_ptr);
extern void OpenNativeFolderPicker(void* web_view_ptr);
extern void ShowNativeAlert(void* web_view_ptr, char* title, char* message);
extern void ShowNativeConfirm(void* web_view_ptr, char* title, char* message);
*/
import "C"

import (
    "encoding/base64"
    "fmt"
    "os"
    "path/filepath"
    "time"
    "unsafe"
    "strings" // ADDED
    "encoding/json" // ADDED
    //"os/exec" // ADDED
    //"bytes" // Add this to your imports if it isn't there!
)
// The Whitelist lives here now - easy to find and edit
var funcWhitelist = map[string]FuncDefinition{
    "OpenNewWindow": {
        Handler: handleOpenNewWindow,
        ArgCount: 5,
        ArgTypes: []string{"string", "string", "string", "int", "int"},
    },
    "SayHello": {
        Handler: handleSayHello,
        ArgCount: 1,
        ArgTypes: []string{"string"},
    },
    "StringSend": {
        Handler: handleStringSend,
        ArgCount: 1,
        ArgTypes: []string{"string"},
    },
    "CloseWindow": {
        Handler: handleCloseNewWindow,
        ArgCount: 1,
        ArgTypes: []string{"string"},
    },
    "ReadFile": {
        Handler: handleReadFile, // Needs to be handleReadFile now
        ArgCount: 1,
        ArgTypes: []string{"string"},
    },
    "WriteFile": {
        Handler: handleWriteFile,
        ArgCount: 2,
        ArgTypes: []string{"string", "string"},
    },
    "DoesFileExist": {
        Handler: handleDoesFileExist,
        ArgCount: 1,
        ArgTypes: []string{"string"},
    },
    "HasTypeExtension": {
        Handler: handleHasTypeExtension,
        ArgCount: 2,
        ArgTypes: []string{"string", "string"},
    },
    "IsFolderLocationReal": {
        Handler: handleIsFolderLocationReal,
        ArgCount: 1,
        ArgTypes: []string{"string"},
    },
    "MakeDirectory": {
        Handler: handleMakeDirectory,
        ArgCount: 1,
        ArgTypes: []string{"string"},
    },
    "GetFolderContentsByPath": {
        Handler: handleGetFolderContentsByPath,
        ArgCount: 1,
        ArgTypes: []string{"string"},
    },
    "GetFolderFoldersByPath": {
        Handler: handleGetFolderFoldersByPath,
        ArgCount: 1,
        ArgTypes: []string{"string"},
    },
    "PickFile": {
        Handler: handlePickFile,
        ArgCount: 0,
        ArgTypes: []string{}, // No arguments needed from the frontend
    },
    "PickFolder": {
        Handler: handlePickFolder,
        ArgCount: 0,
        ArgTypes: []string{}, // No arguments needed from the frontend
    },
    "ShowMessage": {
        Handler: handleShowMessage,
        ArgCount: 2,
        ArgTypes: []string{"string", "string"},
    },
    "ConfirmMessage": {
        Handler: handleConfirmMessage,
        ArgCount: 2,
        ArgTypes: []string{"string", "string"},
    },
}

// --- Internal Helper ---
// This saves you 10 lines of code in every UI-facing handler
func getWebViewByName(name string) unsafe.Pointer {
    for _, win := range GoOpenedWindows {
        if win.WindowName == name {
            return win.webViewPtr
        }
    }
    return nil
}

// --- Your Handlers ---
func handleSayHello(call JSCall) string {
    name := call.Args[0].(string)
    return fmt.Sprintf("Hello, %s!", name)
}

func handleReadFile(call JSCall) string {
    if len(call.Args) < 1 {
        return "false"
    }
    path, ok := call.Args[0].(string)
    if !ok {
        return "false"
    }
    data, err := os.ReadFile(path)
    if err != nil {
        // You can return "false" here, or a specific error string
        // depending on how your frontend handles failures.
        return "false"
    }
    return string(data)
}

func handleStringSend(call JSCall) string {
    if len(call.Args) < 1 {
        return "false"
    }

    // 1. Extract the raw string payload from the arguments
    stringForExe, ok := call.Args[0].(string)
    if !ok {
        return "false"
    }

    // 2. Do whatever Go processing you want here!
    // For now, we will just echo it back to the calling window.
    // You could also save it to a file, log it, or pass it to a database.
    return stringForExe
}

func handleWriteFile(call JSCall) string {
    if len(call.Args) < 2 {
        return "false"
    }
    path, okPath := call.Args[0].(string)
    content, okContent := call.Args[1].(string)
    if !okPath || !okContent {
        return "false"
    }
    // 0644 gives read/write to the owner, and read-only to group/others
    err := os.WriteFile(path, []byte(content), 0644)
    if err != nil {
    // Returning "false" or an empty string on error matches your convention
        return "false"
    }
    return "true" // Or "File saved successfully." depending on what your JS expects!
}

func handleDoesFileExist(call JSCall) string {
    if len(call.Args) < 1 {
        return "false"
    }
    path, ok := call.Args[0].(string)
    if !ok {
        return "false"
    }
    _, err := os.Stat(path)
    if err == nil {
        return "true"
    }
    return "false"
}

func handleHasTypeExtension(call JSCall) string {
    if len(call.Args) < 2 {
        return "false"
    }

    path, ok1 := call.Args[0].(string)
    ext, ok2 := call.Args[1].(string)
    if !ok1 || !ok2 {
        return "false"
    }

    // Get the extension from the path (e.g., ".txt")
    pathExt := filepath.Ext(path)

    // Ensure our target extension has a dot for comparison
    targetExt := ext
    if !strings.HasPrefix(ext, ".") {
        targetExt = "." + ext
    }

    // Compare case-insensitively
    if strings.EqualFold(pathExt, targetExt) {
        return "true"
    }
    return "false"
}

func handleIsFolderLocationReal(call JSCall) string {
    if len(call.Args) < 1 {
        return "false"
    }
    folderLocation, ok := call.Args[0].(string)
    if !ok {
        return "false"
    }
    info, err := os.Stat(folderLocation)
    if err != nil {
        // Path doesn't exist or is inaccessible
        return "false"
    }
    // Return "true" only if it is a Directory
    if info.IsDir() {
        return "true"
    }
    return "false"
}

func handleMakeDirectory(call JSCall) string {
    if len(call.Args) < 1 {
        return "false"
    }
    directoryPath, ok := call.Args[0].(string)
    if !ok {
        return "false"
    }
    // 0755 is the standard permission for directories
    // (Read/Write/Execute for owner, Read/Execute for others)
    err := os.MkdirAll(directoryPath, 0755)
    // If err is nil, it worked (or already existed)
    if err == nil {
        return "true"
    }
    return "false"
}

func handleGetFolderContentsByPath(call JSCall) string {
    if len(call.Args) < 1 {
        return "false"
    }
    folderPath, ok := call.Args[0].(string)
    if !ok {
        return "false"
    }
    entries, err := os.ReadDir(folderPath)
    if err != nil {
        return "false"
    }
    var fileNames []string
    for _, entry := range entries {
        if !entry.IsDir() {
            fileNames = append(fileNames, entry.Name())
        }
    }
    // Manual JSON serialization since we aren't using Wails anymore
    jsonData, err := json.Marshal(fileNames)
    if err != nil {
        return "false"
    }
    return string(jsonData)
}

func handleGetFolderFoldersByPath(call JSCall) string {
    if len(call.Args) < 1 {
        return "false"
    }
    folderPath, ok := call.Args[0].(string)
    if !ok {
        return "false"
    }
    entries, err := os.ReadDir(folderPath)
    if err != nil {
        return "false"
    }
    var folderNames []string
    for _, entry := range entries {
        if entry.IsDir() {
            folderNames = append(folderNames, entry.Name())
        }
    }
    // Manual JSON serialization for the bridge
    jsonData, err := json.Marshal(folderNames)
    if err != nil {
        return "false"
    }
    return string(jsonData)
}

func handlePickFile(call JSCall) string {
    if ptr := getWebViewByName(call.WindowName); ptr != nil {
        C.OpenNativeFilePicker(ptr)
        return "triggered"
    }
    return "false"
}

func handlePickFolder(call JSCall) string {
    if ptr := getWebViewByName(call.WindowName); ptr != nil {
        C.OpenNativeFolderPicker(ptr)
        return "triggered"
    }
    return "false"
}
    
func handleShowMessage(call JSCall) string {
    if len(call.Args) < 2 { return "false" }
    
    title, _ := call.Args[0].(string)
    message, _ := call.Args[1].(string)
    
    if ptr := getWebViewByName(call.WindowName); ptr != nil {
        cTitle := C.CString(title)
        cMsg := C.CString(message)
        defer C.free(unsafe.Pointer(cTitle))
        defer C.free(unsafe.Pointer(cMsg))

        C.ShowNativeAlert(ptr, cTitle, cMsg)
        return "true"
    }
    return "false"
}

func handleConfirmMessage(call JSCall) string {
    if len(call.Args) < 2 { return "false" }
    
    title, _ := call.Args[0].(string)
    message, _ := call.Args[1].(string)

    if ptr := getWebViewByName(call.WindowName); ptr != nil {
        cTitle := C.CString(title)
        cMsg := C.CString(message)
        defer C.free(unsafe.Pointer(cTitle))
        defer C.free(unsafe.Pointer(cMsg))

        C.ShowNativeConfirm(ptr, cTitle, cMsg)
        return "triggered"
    }
    return "false"
}

//-------------------------------------------------

func handleOpenNewWindow(call JSCall) string {
    if len(call.Args) < 5 { return "false" }
    name, _ := call.Args[0].(string)
    addrType, _ := call.Args[1].(string)
    source, _ := call.Args[2].(string)
    w := int(call.Args[3].(float64))
    h := int(call.Args[4].(float64))
    finalSource := source
    if addrType == "HTMLString" {
        decoded, err := base64.StdEncoding.DecodeString(source)
        if err == nil {
            finalSource = string(decoded)
        }
    }
    pwd, _ := os.Getwd()
    baseURI := "file://" + filepath.ToSlash(pwd) + "/"
    cName := C.CString(name)
    cType := C.CString(addrType)
    cSource := C.CString(finalSource)
    cBaseURI := C.CString(baseURI)
    defer C.free(unsafe.Pointer(cName))
    defer C.free(unsafe.Pointer(cType))
    defer C.free(unsafe.Pointer(cSource))
    defer C.free(unsafe.Pointer(cBaseURI))
    ptr := C.OpenPhysicalWindow(cName, cType, cSource, C.int(w), C.int(h), cBaseURI)
    if ptr == nil { return "false" }
    GoOpenedWindows = append(GoOpenedWindows, GoOpenedWindow{
        WindowName: name,
        TimeStamp: time.Now().Format("2006-01-02 15:04:05"),
        Width: w,
        Height: h,
        webViewPtr: ptr,
    })
    return "true"
}

func handleCloseNewWindow(call JSCall) string {
    if len(call.Args) < 1 { return "false" }
    targetName := call.Args[0].(string)
    
    indexNum := -1
    for i, win := range GoOpenedWindows {
        if win.WindowName == targetName {
            indexNum = i
            break
        }
    }
    
    if indexNum == -1 { return "false" }

    cName := C.CString(targetName)
    defer C.free(unsafe.Pointer(cName))
    
    // We cast the C.bool to Go bool for the if-statement
    if bool(C.ClosePhysicalWindow(cName)) {
        // Remove from slice
        GoOpenedWindows = append(GoOpenedWindows[:indexNum], GoOpenedWindows[indexNum+1:]...)
        return "true"
    }
    return "false"
}
