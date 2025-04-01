package main

import (
	"fmt"
	"log"
	"os"
)

func main() {
	if len(os.Args) < 2 {
		fmt.Println("Usage: graphdata <repo-directory>")
		os.Exit(1)
	}
	repoDir := os.Args[1]

	// Detect languages used in the repository.
	languages, err := DetectLanguages(repoDir)
	if err != nil {
		log.Fatalf("Error detecting languages: %v", err)
	}
	fmt.Printf("Detected languages: %v\n", languages)

	// Find entry points based on the detected languages.
	files, err := FindEntryPoints(repoDir, languages)
	if err != nil {
		log.Fatalf("Error scanning directory: %v", err)
	}
	if len(files) == 0 {
		log.Fatal("No entry point files found in the provided directory.")
	}

	// Read package.json dependencies if JavaScript is detected.
	var pkgDeps map[string]bool
	if contains(languages, "javascript") {
		pkgDeps, err = ReadPackageJSON(repoDir)
		if err != nil {
			log.Printf("Warning: could not parse package.json: %v", err)
		}
	}

	// Build a dependency tree by scanning each file.
	depTree := make(map[string][]string)
	// Accumulate JS analysis reports.
	var jsReports []JSAnalysisReport

	for _, file := range files {
		deps, err := ScanDependencies(file)
		if err != nil {
			log.Printf("Error scanning %s: %v", file, err)
			continue
		}
		depTree[file] = deps
		// Optionally check import/export specifics.
		CheckImportsExports(file, languageForFile(file))

		// If the file is JavaScript, perform advanced analysis.
		if languageForFile(file) == "javascript" {
			report, err := AnalyzeJSFile(file)
			if err != nil {
				log.Printf("Error analyzing JS file %s: %v", file, err)
			} else {
				jsReports = append(jsReports, *report)
			}
		}
	}

	// Build the dependency graph.
	graph, err := BuildGraph(depTree, pkgDeps)
	if err != nil {
		log.Fatalf("Error building graph: %v", err)
	}

	// Save the dependency graph as JSON.
	err = SaveGraphData("graphdata.json", graph)
	if err != nil {
		log.Fatalf("Error writing graph data: %v", err)
	}
	fmt.Println("Graph data saved to graphdata.json")

	// Save the aggregated JS analysis reports as JSON.
	err = SaveJSAnalysisReports(jsReports, "js_analysis_data.json")
	if err != nil {
		log.Fatalf("Error writing JS analysis data: %v", err)
	}
	fmt.Println("JS analysis data saved to js_analysis_data.json")
}

func contains(slice []string, str string) bool {
	for _, s := range slice {
		if s == str {
			return true
		}
	}
	return false
}

func languageForFile(file string) string {
	if hasSuffix(file, ".js") || hasSuffix(file, ".ts") || hasSuffix(file, ".jsx") || hasSuffix(file, ".tsx") {
		return "javascript"
	} else if hasSuffix(file, ".py") {
		return "python"
	} else if hasSuffix(file, ".go") {
		return "go"
	}
	return "unknown"
}
