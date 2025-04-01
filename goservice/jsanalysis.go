package main

import (
	"encoding/json"
	"io/ioutil"
	"os"
	"regexp"
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

// VariableSymbol holds variable information and its transformations.
type VariableSymbol struct {
	Name            string           `json:"name"`
	DefinedAtLine   int              `json:"defined_at_line"`
	Transformations []Transformation `json:"transformations"`
	UsedInFiles     []string         `json:"used_in_files"`
}

// JSAnalysisReport holds the analysis report for a JS file.
type JSAnalysisReport struct {
	File      string           `json:"file"`
	Functions []FunctionSymbol `json:"functions"`
	Variables []VariableSymbol `json:"variables"`
}

// AnalyzeJSFile analyzes a JS file to extract functions and variable usages.
func AnalyzeJSFile(filePath string) (*JSAnalysisReport, error) {
	content, err := ioutil.ReadFile(filePath)
	if err != nil {
		return nil, err
	}
	lines := regexp.MustCompile("\r?\n").Split(string(content), -1)

	// First pass: collect all user-defined function names.
	reFunc := regexp.MustCompile(`function\s+(\w+)\s*\(`)
	userDefined := make(map[string]bool)
	for _, line := range lines {
		if matches := reFunc.FindStringSubmatch(line); matches != nil {
			userDefined[matches[1]] = true
		}
	}

	var functions []FunctionSymbol
	var variables []VariableSymbol

	// Regex for variable declarations (var, let, or const).
	reVar := regexp.MustCompile(`(var|let|const)\s+(\w+)`)
	for i, line := range lines {
		// Process function definitions.
		if matches := reFunc.FindStringSubmatch(line); matches != nil {
			fname := matches[1]
			functions = append(functions, FunctionSymbol{
				Name:      fname,
				StartLine: i + 1,
				EndLine:   i + 1, // EndLine is a placeholder.
				Calls:     extractFunctionCalls(userDefined, fname, lines),
			})
		}
		// Process variable declarations.
		if matches := reVar.FindStringSubmatch(line); matches != nil {
			vname := matches[2]
			variables = append(variables, VariableSymbol{
				Name:            vname,
				DefinedAtLine:   i + 1,
				Transformations: extractVariableTransformations(filePath, vname, lines),
				UsedInFiles:     []string{}, // Populate with cross-file usage if needed.
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
		// Find all potential call matches.
		matches := reCall.FindAllStringSubmatch(line, -1)
		for _, m := range matches {
			callName := m[1]
			// Skip if the call is to itself or not in the user-defined map.
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
	reAssign := regexp.MustCompile(varName + `\s*=`)
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

// SaveJSAnalysisReport writes a single-file analysis report to a JSON file.
func SaveJSAnalysisReport(report *JSAnalysisReport, outputPath string) error {
	data, err := json.MarshalIndent(report, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(outputPath, data, 0644)
}

// SaveJSAnalysisReports writes the aggregated analysis reports to a JSON file.
func SaveJSAnalysisReports(reports []JSAnalysisReport, outputPath string) error {
	data, err := json.MarshalIndent(reports, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(outputPath, data, 0644)
}
