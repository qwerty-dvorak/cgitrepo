package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"os"
	"regexp"
	"time"

	"github.com/sourcegraph/jsonrpc2"
)

type GoFunctionSymbol struct {
	Name      string       `json:"name"`
	StartLine int          `json:"start_line"`
	EndLine   int          `json:"end_line"`
	Calls     []CallDetail `json:"calls"`
}

type GoVariableSymbol struct {
	Name          string   `json:"name"`
	DefinedAtLine int      `json:"defined_at_line"`
	UsedInFiles   []string `json:"used_in_files"`
}

type GoAnalysisReport struct {
	File      string             `json:"file"`
	Functions []GoFunctionSymbol `json:"functions"`
	Variables []GoVariableSymbol `json:"variables"`
}

func AnalyzeGoFileLSP(filePath string) (*GoAnalysisReport, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()

	// Connect to the LSP server.
	netConn, err := net.Dial("tcp", "localhost:2087")
	if err != nil {
		// Fallback if LSP is not available.
		return AnalyzeGoFile(filePath)
	}

	stream := jsonrpc2.NewBufferedStream(netConn, jsonrpc2.VSCodeObjectCodec{})
	conn := jsonrpc2.NewConn(ctx, stream, jsonrpc2.HandlerWithError(func(ctx context.Context, conn *jsonrpc2.Conn, req *jsonrpc2.Request) (interface{}, error) {
		return nil, nil
	}))
	defer conn.Close()

	// Initialize the LSP handshake.
	initParams := map[string]interface{}{
		"processId":    os.Getpid(),
		"rootUri":      fmt.Sprintf("file://%s", RepoDir),
		"capabilities": map[string]interface{}{},
	}
	var initResult interface{}
	if err := conn.Call(ctx, "initialize", initParams, &initResult); err != nil {
		return AnalyzeGoFile(filePath)
	}
	conn.Notify(ctx, "initialized", struct{}{})

	// Open the document with languageId "go".
	content, err := os.ReadFile(filePath)
	if err != nil {
		return nil, err
	}
	didOpen := map[string]interface{}{
		"textDocument": map[string]interface{}{
			"uri":        filePath,
			"languageId": "go",
			"version":    1,
			"text":       string(content),
		},
	}
	conn.Notify(ctx, "textDocument/didOpen", didOpen)

	// Request document symbols.
	symbolParams := map[string]interface{}{
		"textDocument": map[string]interface{}{
			"uri": filePath,
		},
	}
	var symbols []interface{}
	if err := conn.Call(ctx, "textDocument/documentSymbol", symbolParams, &symbols); err != nil {
		return AnalyzeGoFile(filePath)
	}

	// Basic fallback parsing for functions and variables.
	lines := regexp.MustCompile("\r?\n").Split(string(content), -1)
	// For functions in Go: expecting lines starting with "func"
	regexp.MustCompile(`^func\s+(\w+)`)
	// For variable declarations, handle both "var ..." and short declarations ":="
	reVar := regexp.MustCompile(`\b(var\s+(\w+)|(\w+)\s*:=)`)

	var functions []GoFunctionSymbol
	var variables []GoVariableSymbol

	// Map functions from LSP symbols.
	for _, symItem := range symbols {
		if sym, ok := symItem.(map[string]interface{}); ok {
			name, _ := sym["name"].(string)
			startLine, endLine := extractRange(sym)
			functions = append(functions, GoFunctionSymbol{
				Name:      name,
				StartLine: startLine,
				EndLine:   endLine,
				Calls:     []CallDetail{}, // advanced call extraction can be added here.
			})
		}
	}

	// Scan for variable declarations.
	for i, line := range lines {
		if matches := reVar.FindStringSubmatch(line); matches != nil {
			vname := ""
			if matches[2] != "" {
				vname = matches[2]
			} else if matches[3] != "" {
				vname = matches[3]
			}
			if vname == "" {
				continue
			}
			// Naively get usage locations (fallback across repo).
			usageList, err := FindVariableUsageAcrossRepo(vname)
			if err != nil {
				log.Printf("Error finding usage for variable %s: %v", vname, err)
			}
			variables = append(variables, GoVariableSymbol{
				Name:          vname,
				DefinedAtLine: i + 1,
				UsedInFiles:   usageList,
			})
		}
	}

	report := &GoAnalysisReport{
		File:      filePath,
		Functions: functions,
		Variables: variables,
	}
	report.File += " (Go analyzed by LSP)"
	return report, nil
}

func AnalyzeGoFile(filePath string) (*GoAnalysisReport, error) {
	content, err := os.ReadFile(filePath)
	if err != nil {
		return nil, err
	}
	lines := regexp.MustCompile("\r?\n").Split(string(content), -1)
	reFunc := regexp.MustCompile(`^func\s+(\w+)`)
	reVar := regexp.MustCompile(`\b(var\s+(\w+)|(\w+)\s*:=)`)

	var functions []GoFunctionSymbol
	var variables []GoVariableSymbol

	for i, line := range lines {
		if matches := reFunc.FindStringSubmatch(line); matches != nil {
			functions = append(functions, GoFunctionSymbol{
				Name:      matches[1],
				StartLine: i + 1,
				EndLine:   i + 1, // placeholder for end line.
				Calls:     []CallDetail{},
			})
		}
		if matches := reVar.FindStringSubmatch(line); matches != nil {
			vname := ""
			if matches[2] != "" {
				vname = matches[2]
			} else if matches[3] != "" {
				vname = matches[3]
			}
			if vname == "" {
				continue
			}
			usageList, err := FindVariableUsageAcrossRepo(vname)
			if err != nil {
				log.Printf("Error finding usage for variable %s: %v", vname, err)
			}
			variables = append(variables, GoVariableSymbol{
				Name:          vname,
				DefinedAtLine: i + 1,
				UsedInFiles:   usageList,
			})
		}
	}

	report := &GoAnalysisReport{
		File:      filePath,
		Functions: functions,
		Variables: variables,
	}
	return report, nil
}

// SaveGoAnalysisReports writes the Go analysis reports to a JSON file.
func SaveGoAnalysisReports(reports []GoAnalysisReport, outputPath string) error {
	data, err := json.MarshalIndent(reports, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(outputPath, data, 0644)
}

// -----------------------
// Advanced Python Analysis (LSP)
// -----------------------

type PythonFunctionSymbol struct {
	Name      string `json:"name"`
	StartLine int    `json:"start_line"`
	EndLine   int    `json:"end_line"`
}

type PythonVariableSymbol struct {
	Name          string `json:"name"`
	DefinedAtLine int    `json:"defined_at_line"`
}

type PythonAnalysisReport struct {
	File      string                 `json:"file"`
	Functions []PythonFunctionSymbol `json:"functions"`
	Variables []PythonVariableSymbol `json:"variables"`
}

func AnalyzePythonFileLSP(filePath string) (*PythonAnalysisReport, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()

	// Connect to the LSP server.
	netConn, err := net.Dial("tcp", "localhost:2087")
	if err != nil {
		return AnalyzePythonFile(filePath)
	}

	stream := jsonrpc2.NewBufferedStream(netConn, jsonrpc2.VSCodeObjectCodec{})
	conn := jsonrpc2.NewConn(ctx, stream, jsonrpc2.HandlerWithError(func(ctx context.Context, conn *jsonrpc2.Conn, req *jsonrpc2.Request) (interface{}, error) {
		return nil, nil
	}))
	defer conn.Close()

	// Initialize LSP handshake.
	initParams := map[string]interface{}{
		"processId":    os.Getpid(),
		"rootUri":      fmt.Sprintf("file://%s", RepoDir),
		"capabilities": map[string]interface{}{},
	}
	var initResult interface{}
	if err := conn.Call(ctx, "initialize", initParams, &initResult); err != nil {
		return AnalyzePythonFile(filePath)
	}
	conn.Notify(ctx, "initialized", struct{}{})

	// Open the document with languageId "python".
	content, err := os.ReadFile(filePath)
	if err != nil {
		return nil, err
	}
	didOpen := map[string]interface{}{
		"textDocument": map[string]interface{}{
			"uri":        filePath,
			"languageId": "python",
			"version":    1,
			"text":       string(content),
		},
	}
	conn.Notify(ctx, "textDocument/didOpen", didOpen)

	// Request document symbols.
	symbolParams := map[string]interface{}{
		"textDocument": map[string]interface{}{
			"uri": filePath,
		},
	}
	var symbols []interface{}
	if err := conn.Call(ctx, "textDocument/documentSymbol", symbolParams, &symbols); err != nil {
		return AnalyzePythonFile(filePath)
	}

	// Basic internal analysis.
	lines := regexp.MustCompile("\r?\n").Split(string(content), -1)
	// For functions in Python.
	regexp.MustCompile(`^def\s+(\w+)\s*\(`)
	// For variable assignments; this is a naive approach.
	reVar := regexp.MustCompile(`^(\w+)\s*=`)
	var functions []PythonFunctionSymbol
	var variables []PythonVariableSymbol

	for _, symItem := range symbols {
		if sym, ok := symItem.(map[string]interface{}); ok {
			name, _ := sym["name"].(string)
			startLine, endLine := extractRange(sym)
			functions = append(functions, PythonFunctionSymbol{
				Name:      name,
				StartLine: startLine,
				EndLine:   endLine,
			})
		}
	}

	for i, line := range lines {
		if matches := reVar.FindStringSubmatch(line); matches != nil {
			variables = append(variables, PythonVariableSymbol{
				Name:          matches[1],
				DefinedAtLine: i + 1,
			})
		}
	}

	report := &PythonAnalysisReport{
		File:      filePath,
		Functions: functions,
		Variables: variables,
	}
	report.File += " (Python analyzed by LSP)"
	return report, nil
}

func AnalyzePythonFile(filePath string) (*PythonAnalysisReport, error) {
	content, err := os.ReadFile(filePath)
	if err != nil {
		return nil, err
	}
	lines := regexp.MustCompile("\r?\n").Split(string(content), -1)
	reFunc := regexp.MustCompile(`^def\s+(\w+)\s*\(`)
	reVar := regexp.MustCompile(`^(\w+)\s*=`)
	var functions []PythonFunctionSymbol
	var variables []PythonVariableSymbol

	for i, line := range lines {
		if matches := reFunc.FindStringSubmatch(line); matches != nil {
			functions = append(functions, PythonFunctionSymbol{
				Name:      matches[1],
				StartLine: i + 1,
				EndLine:   i + 1, // placeholder
			})
		}
		if matches := reVar.FindStringSubmatch(line); matches != nil {
			variables = append(variables, PythonVariableSymbol{
				Name:          matches[1],
				DefinedAtLine: i + 1,
			})
		}
	}

	report := &PythonAnalysisReport{
		File:      filePath,
		Functions: functions,
		Variables: variables,
	}
	return report, nil
}

// SavePyAnalysisReports writes the Python analysis reports to a JSON file.
func SavePyAnalysisReports(reports []PythonAnalysisReport, outputPath string) error {
	data, err := json.MarshalIndent(reports, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(outputPath, data, 0644)
}
