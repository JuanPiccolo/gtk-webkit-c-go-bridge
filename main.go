package main

/*
#cgo pkg-config: gtk+-3.0 webkit2gtk-4.1
#include <stdlib.h>
#include <stdbool.h>
extern void* OpenPhysicalWindow(char* name, char* type, char* source, int w, int h, char* fileloc);
extern bool ClosePhysicalWindow(char* name);
extern void RunJavaScriptInWindow(void* web_view_ptr, char* js_code);
extern bool RunJavaScriptByWindowName(char* name, char* js_code);
extern void OpenNativeFilePicker(void* web_view_ptr);
*/
import "C"

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"
	"unsafe"
)

// --- BRIDGE TYPES ---

type JSCall struct {
	ID         int      `json:"id"`
	FuncName   string   `json:"funcName"`
	ArbAddress string   `json:"arbAddress"`
	WindowName string   `json:"windowName"`
	Args       []any    `json:"args"`
	Types      []string `json:"types"`
}

type GoResponse struct {
	ID     int    `json:"id"`
	Result string `json:"result"`
}

type GoOpenedWindow struct {
	WindowName string
	TimeStamp  string
	Width      int
	Height     int
	webViewPtr unsafe.Pointer
}

var GoOpenedWindows []GoOpenedWindow

const MainWindowLabel = "Main Window"

// Shared Type Definitions for handlers.go
type AuthorizedFunc func(JSCall) string
type FuncDefinition struct {
	Handler  AuthorizedFunc
	ArgCount int
	ArgTypes []string
}

// --- WHITELISTED HANDLERS ---

//export GoAppActivate
func GoAppActivate() {
	pwd, _ := os.Getwd()
	baseURI := "file://" + filepath.ToSlash(pwd) + "/"

	rawName := MainWindowLabel
	
	cWindowName := C.CString(rawName)
	cAddrType := C.CString("HTMLAddress")
	cSource := C.CString("file://" + filepath.Join(pwd, "index.html"))
	cFileloc := C.CString(baseURI)

	defer C.free(unsafe.Pointer(cWindowName))
	defer C.free(unsafe.Pointer(cAddrType))
	defer C.free(unsafe.Pointer(cSource))
	defer C.free(unsafe.Pointer(cFileloc))

	ptr := C.OpenPhysicalWindow(cWindowName, cAddrType, cSource, 1024, 768, cFileloc)

	if ptr != nil {
		GoOpenedWindows = append(GoOpenedWindows, GoOpenedWindow{
			WindowName: rawName,
			TimeStamp:  time.Now().Format("2006-01-02 15:04:05"),
			Width:      1024,
			Height:     768,
			webViewPtr: ptr,
		})
	}
}

//export GoTrafficCop
func GoTrafficCop(rawJSON *C.char) *C.char {
	input := C.GoString(rawJSON)
	var call JSCall
	if err := json.Unmarshal([]byte(input), &call); err != nil {
		return createErrorResponse(0, "Packet Mangle Error")
	}

	def, exists := funcWhitelist[call.FuncName]
	if !exists {
		return createErrorResponse(call.ID, "Unauthorized Call: "+call.FuncName)
	}

	if len(call.Args) != def.ArgCount {
		return createErrorResponse(call.ID, fmt.Sprintf("Argument Count Mismatch: Expected %d", def.ArgCount))
	}

	for i, expectedType := range def.ArgTypes {
		if !validateType(call.Args[i], expectedType) {
			return createErrorResponse(call.ID, fmt.Sprintf("Data Integrity Error: Arg[%d] is not %s", i, expectedType))
		}
	}

	if call.WindowName != "" && call.WindowName != MainWindowLabel {
		if len(call.Args) > 0 {
			if jsCommand, ok := call.Args[0].(string); ok {
				cTarget := C.CString(call.WindowName)
				cJS := C.CString(jsCommand)
				defer C.free(unsafe.Pointer(cTarget))
				defer C.free(unsafe.Pointer(cJS))
				
				success := C.RunJavaScriptByWindowName(cTarget, cJS)
				return createResponse(call.ID, fmt.Sprintf("%t", bool(success)))
			}
		}
	}

	result := def.Handler(call)
	return createResponse(call.ID, result)
}

// --- HELPERS ---

func validateType(val any, expected string) bool {
	switch expected {
	case "string":
		_, ok := val.(string)
		return ok
	case "int":
		f, ok := val.(float64)
		return ok && f == float64(int(f))
	case "float64":
		_, ok := val.(float64)
		return ok
	case "bool":
		_, ok := val.(bool)
		return ok
	default:
		return false
	}
}

func createResponse(id int, result string) *C.char {
	resp := GoResponse{ID: id, Result: result}
	jsonBytes, _ := json.Marshal(resp)
	return C.CString(base64.StdEncoding.EncodeToString(jsonBytes))
}

func createErrorResponse(id int, message string) *C.char {
	return createResponse(id, "Error: "+message)
}

func main() {}