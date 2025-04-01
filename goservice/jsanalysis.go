package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"github.com/sourcegraph/jsonrpc2"
)

// CallDetail holds details about a function call.
type CallDetail struct {
	Name string `json:"name"`
	Line int    `json:"line"`
}

// Transformation holds information about a variable re-assignment.
type Transformation struct {
	File string `json:"file"`
	Line int    `json:"line"`
}

// FunctionSymbol holds information about a function.
type FunctionSymbol struct {
	Name      string       `json:"name"`
	StartLine int          `json:"start_line"`
	EndLine   int          `json:"end_line"`
	Calls     []CallDetail `json:"calls"`
}

// VariableSymbol holds variable information, its transformations, and its usage (which may be nested).
type VariableSymbol struct {
	Name            string           `json:"name"`
	DefinedAtLine   int              `json:"defined_at_line"`
	Transformations []Transformation `json:"transformations"`
	// Ordered usage locations (file:line) tracking
	UsedInFiles []string `json:"used_in_files"`
}

// JSAnalysisReport holds the analysis report for a single JS file.
type JSAnalysisReport struct {
	File      string           `json:"file"`
	Functions []FunctionSymbol `json:"functions"`
	Variables []VariableSymbol `json:"variables"`
}

// -----------------------
// LSP Integration Helpers
// -----------------------

// lspRequest sends an LSP call using the provided connection.
func lspRequest(ctx context.Context, conn *jsonrpc2.Conn, method string, params interface{}, result interface{}) error {
	return conn.Call(ctx, method, params, result)
}

// FindVariableReferencesLSP determines the references for a variable (given its file, line and character)
// by calling the "textDocument/references" method. Note that the LSP server must support this.
func FindVariableReferencesLSP(ctx context.Context, conn *jsonrpc2.Conn, file string, line, character int) ([]string, error) {
	// Prepare the reference parameters per LSP spec.
	params := map[string]interface{}{
		"textDocument": map[string]interface{}{
			"uri": file,
		},
		"position": map[string]interface{}{
			"line":      line,
			"character": character,
		},
		"context": map[string]interface{}{
			"includeDeclaration": true,
		},
	}
	var refs []struct {
		Uri   string `json:"uri"`
		Range struct {
			Start struct {
				Line      int `json:"line"`
				Character int `json:"character"`
			} `json:"start"`
		} `json:"range"`
	}
	if err := lspRequest(ctx, conn, "textDocument/references", params, &refs); err != nil {
		return nil, err
	}
	var locations []string
	for _, ref := range refs {
		// Format each reference as "filepath:line".
		locations = append(locations, fmt.Sprintf("%s:%d", ref.Uri, ref.Range.Start.Line+1))
	}
	return locations, nil
}

// -----------------------
// File analysis functions
// -----------------------

// AnalyzeJSFileLSP uses LSP integration to analyze a JavaScript file fully.
// It requests both document symbols and variable references so that each variable’s
// UsedInFiles field is populated with the ordered usage paths across the repo.
func AnalyzeJSFileLSP(filePath string) (*JSAnalysisReport, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()

	// Connect to the LSP server.
	netConn, err := net.Dial("tcp", "localhost:2087")
	if err != nil {
		// Fallback to our internal analysis if LSP is not available.
		return AnalyzeJSFile(filePath)
	}

	stream := jsonrpc2.NewBufferedStream(netConn, jsonrpc2.VSCodeObjectCodec{})
	conn := jsonrpc2.NewConn(ctx, stream, jsonrpc2.HandlerWithError(func(ctx context.Context, conn *jsonrpc2.Conn, req *jsonrpc2.Request) (interface{}, error) {
		return nil, nil
	}))
	defer conn.Close()

	// 1. Initialize LSP handshake.
	initParams := map[string]interface{}{
		"processId":    os.Getpid(),
		"rootUri":      fmt.Sprintf("file://%s", RepoDir),
		"capabilities": map[string]interface{}{},
	}
	var initResult interface{}
	if err := conn.Call(ctx, "initialize", initParams, &initResult); err != nil {
		return AnalyzeJSFile(filePath)
	}
	conn.Notify(ctx, "initialized", struct{}{})

	// 2. Open the document.
	content, err := os.ReadFile(filePath)
	if err != nil {
		return nil, err
	}
	didOpen := map[string]interface{}{
		"textDocument": map[string]interface{}{
			"uri":        filePath,
			"languageId": "javascript",
			"version":    1,
			"text":       string(content),
		},
	}
	conn.Notify(ctx, "textDocument/didOpen", didOpen)

	// 3. Request document symbols.
	symbolParams := map[string]interface{}{
		"textDocument": map[string]interface{}{
			"uri": filePath,
		},
	}
	var symbols []interface{}
	if err := conn.Call(ctx, "textDocument/documentSymbol", symbolParams, &symbols); err != nil {
		// Fallback if there is an error.
		return AnalyzeJSFile(filePath)
	}

	// Now do a basic internal analysis to capture variable definitions.
	lines := regexp.MustCompile("\r?\n").Split(string(content), -1)
	reVar := regexp.MustCompile(`(var|let|const)\s+(\w+)`)

	var variables []VariableSymbol

	// For each variable declaration, use LSP references to get its usage.
	for i, line := range lines {
		if matches := reVar.FindStringSubmatch(line); matches != nil {
			vname := matches[2]

			// Find the column (naively using indexOf)
			col := strings.Index(line, vname)
			usageList, err := FindVariableReferencesLSP(ctx, conn, filePath, i, col)
			if err != nil {
				log.Printf("Error finding references for variable %s in %s:%d: %v", vname, filePath, i+1, err)
			}
			transforms := extractVariableTransformations(filePath, vname, lines)
			variables = append(variables, VariableSymbol{
				Name:            vname,
				DefinedAtLine:   i + 1,
				Transformations: transforms,
				UsedInFiles:     usageList,
			})
		}
	}

	// For functions, we map document symbols (assumed to be functions) into our structure.
	functions := mapSymbolsToFunctions(symbols)

	report := &JSAnalysisReport{
		File:      filePath,
		Functions: functions,
		Variables: variables,
	}

	// Optional: Shutdown LSP connection gracefully.
	var shutdownResult interface{}
	_ = conn.Call(ctx, "shutdown", nil, &shutdownResult)
	conn.Notify(ctx, "exit", nil)

	report.File += " (analyzed by LSP)"
	return report, nil
}

// AnalyzeJSFile is our fallback analysis (using regex scanning) if LSP isn’t available.
func AnalyzeJSFile(filePath string) (*JSAnalysisReport, error) {
	content, err := os.ReadFile(filePath)
	if err != nil {
		return nil, err
	}
	lines := regexp.MustCompile("\r?\n").Split(string(content), -1)

	reFunc := regexp.MustCompile(`function\s+(\w+)\s*\(`)
	userDefined := make(map[string]bool)
	for _, line := range lines {
		if matches := reFunc.FindStringSubmatch(line); matches != nil {
			userDefined[matches[1]] = true
		}
	}

	var functions []FunctionSymbol
	var variables []VariableSymbol
	reVar := regexp.MustCompile(`(var|let|const)\s+(\w+)`)

	for i, line := range lines {
		if matches := reFunc.FindStringSubmatch(line); matches != nil {
			fname := matches[1]
			functions = append(functions, FunctionSymbol{
				Name:      fname,
				StartLine: i + 1,
				EndLine:   i + 1, // placeholder
				Calls:     extractFunctionCalls(userDefined, fname, lines),
			})
		}
		if matches := reVar.FindStringSubmatch(line); matches != nil {
			vname := matches[2]
			usageList, err := FindVariableUsageAcrossRepo(vname)
			if err != nil {
				log.Printf("Error finding usage for variable %s: %v", vname, err)
			}
			variables = append(variables, VariableSymbol{
				Name:            vname,
				DefinedAtLine:   i + 1,
				Transformations: extractVariableTransformations(filePath, vname, lines),
				UsedInFiles:     usageList,
			})
		}
	}

	report := &JSAnalysisReport{
		File:      filePath,
		Functions: functions,
		Variables: variables,
	}
	return report, nil
}

// extractFunctionCalls scans the file lines to extract call details from user-defined functions.
func extractFunctionCalls(userDefined map[string]bool, currentFunc string, lines []string) []CallDetail {
	var calls []CallDetail
	reCall := regexp.MustCompile(`(\w+)\s*\(`)
	for i, line := range lines {
		matches := reCall.FindAllStringSubmatch(line, -1)
		for _, m := range matches {
			callName := m[1]
			if callName == currentFunc {
				continue
			}
			if _, exists := userDefined[callName]; !exists {
				continue
			}
			calls = append(calls, CallDetail{
				Name: callName,
				Line: i + 1,
			})
		}
	}
	return calls
}

// extractVariableTransformations locates where a variable is re-assigned.
func extractVariableTransformations(filePath, varName string, lines []string) []Transformation {
	var transforms []Transformation
	reAssign := regexp.MustCompile(fmt.Sprintf(`\b%s\s*=`, regexp.QuoteMeta(varName)))
	for i, line := range lines {
		if reAssign.MatchString(line) {
			transforms = append(transforms, Transformation{
				File: filePath,
				Line: i + 1,
			})
		}
	}
	return transforms
}

// FindVariableUsageAcrossRepo scans all JS, JSX, TS, and TSX files under RepoDir
// using a simple regex and returns matching usage locations.
func FindVariableUsageAcrossRepo(varName string) ([]string, error) {
	var usages []string
	pattern := fmt.Sprintf(`\b%s\b`, regexp.QuoteMeta(varName))
	reUsage := regexp.MustCompile(pattern)

	err := filepath.Walk(RepoDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			log.Printf("Error accessing %s: %v", path, err)
			return nil
		}
		if info.IsDir() {
			return nil
		}
		// Consider only JavaScript-related files.
		if !(strings.HasSuffix(path, ".js") ||
			strings.HasSuffix(path, ".jsx") ||
			strings.HasSuffix(path, ".ts") ||
			strings.HasSuffix(path, ".tsx")) {
			return nil
		}
		content, err := os.ReadFile(path)
		if err != nil {
			log.Printf("Error reading %s: %v", path, err)
			return nil
		}
		lines := regexp.MustCompile("\r?\n").Split(string(content), -1)
		for i, line := range lines {
			if reUsage.MatchString(line) {
				usages = append(usages, fmt.Sprintf("%s:%d", path, i+1))
			}
		}
		return nil
	})
	return usages, err
}

// mapSymbolsToFunctions converts LSP document symbols into our FunctionSymbol structure.
func mapSymbolsToFunctions(symbols []interface{}) []FunctionSymbol {
	var functions []FunctionSymbol
	for _, symItem := range symbols {
		if sym, ok := symItem.(map[string]interface{}); ok {
			name, _ := sym["name"].(string)
			startLine, endLine := extractRange(sym)
			functions = append(functions, FunctionSymbol{
				Name:      name,
				StartLine: startLine,
				EndLine:   endLine,
				Calls:     []CallDetail{},
			})
		}
	}
	return functions
}

// extractRange extracts start and end lines from an LSP symbol.
func extractRange(sym map[string]interface{}) (int, int) {
	if r, ok := sym["range"].(map[string]interface{}); ok {
		if start, ok := r["start"].(map[string]interface{}); ok {
			if end, ok := r["end"].(map[string]interface{}); ok {
				startLine, _ := start["line"].(float64)
				endLine, _ := end["line"].(float64)
				return int(startLine) + 1, int(endLine) + 1
			}
		}
	}
	return 0, 0
}

// SaveJSAnalysisReports writes the aggregated analysis reports to a JSON file.
func SaveJSAnalysisReports(reports []JSAnalysisReport, outputPath string) error {
	data, err := json.MarshalIndent(reports, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(outputPath, data, 0644)
}
