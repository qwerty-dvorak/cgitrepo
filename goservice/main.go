package main

import (
	"fmt"
	"log"
	"os"
	"path/filepath"
)

// Global repo directory
var RepoDir string

func main() {
	if len(os.Args) < 2 {
		fmt.Println("Usage: graphdata <repo-directory>")
		os.Exit(1)
	}
	repoDir := os.Args[1]
	RepoDir = repoDir

	// Detect languages used in the repository.
	languages, err := DetectLanguages(repoDir)
	if err != nil {
		log.Fatalf("Error detecting languages: %v", err)
	}
	fmt.Printf("Detected languages: %v\n", languages)

	// Determine the main language.
	mainLanguage := "unknown"
	if len(languages) > 0 {
		mainLanguage = languages[0] // Assuming the first detected language is the main one
	}

	// Find entry points based on the detected languages.
	files, err := FindEntryPoints(repoDir, languages)
	if err != nil {
		log.Fatalf("Error scanning directory: %v", err)
	}
	if len(files) == 0 {
		log.Fatal("No entry point files found in the provided directory.")
	}

	// Check for Prisma schema file.
	prismaSchemaPath := ""
	err = filepath.Walk(repoDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if !info.IsDir() && filepath.Base(path) == "schema.prisma" {
			prismaSchemaPath = path
			return filepath.SkipDir // Stop after finding the first schema.prisma
		}
		return nil
	})
	if err != nil {
		log.Printf("Error while searching for Prisma schema: %v", err)
	}
	if prismaSchemaPath != "" {
		fmt.Println("Found Prisma schema at:", prismaSchemaPath)
		// Add the Prisma schema to our files list.
		files = append(files, prismaSchemaPath)
	}

	// Read package.json dependencies if JavaScript is detected.
	var pkgDeps map[string]bool
	jsDetected := contains(languages, "javascript") ||
		contains(languages, "typescript") ||
		contains(languages, "jsx") ||
		contains(languages, "tsx")

	if jsDetected {
		pkgDeps, err = ReadPackageJSON(repoDir)
		if err != nil {
			log.Printf("Warning: could not parse package.json: %v", err)
		}
	}

	// Build a dependency tree by scanning each file.
	depTree := make(map[string][]string)
	// Accumulate analysis reports.
	var jsReports []JSAnalysisReport
	var goReports []GoAnalysisReport
	var pyReports []PythonAnalysisReport
	// Track if we found a Prisma schema for inclusion in the graph.
	var prismaSchema *PrismaSchema

	for _, file := range files {
		// Use the ScanDependencies function from dependency_scanner.go.
		deps, err := ScanDependencies(file)
		if err != nil {
			log.Printf("Error scanning %s: %v", file, err)
			continue
		}
		depTree[file] = deps
		// Optionally check import/export specifics.
		CheckImportsExports(file, languageForFile(file))

		// Within the file scanning loop:
		fileLanguage := languageForFile(file)
		if fileLanguage == "javascript" || fileLanguage == "typescript" ||
			fileLanguage == "jsx" || fileLanguage == "tsx" {
			report, err := AnalyzeJSFileLSP(file)
			if err != nil {
				log.Printf("Error analyzing JS file %s: %v", file, err)
			} else {
				jsReports = append(jsReports, *report)
			}
		} else if fileLanguage == "go" {
			report, err := AnalyzeGoFileLSP(file)
			if err != nil {
				log.Printf("Error analyzing Go file %s: %v", file, err)
			} else {
				goReports = append(goReports, *report)
			}
		} else if fileLanguage == "python" {
			report, err := AnalyzePythonFileLSP(file)
			if err != nil {
				log.Printf("Error analyzing Python file %s: %v", file, err)
			} else {
				pyReports = append(pyReports, *report)
			}
		} else if fileLanguage == "prisma" {
			// Read and parse Prisma schema.
			schema, err := ParsePrismaSchema(file)
			if err != nil {
				log.Printf("Error parsing Prisma schema %s: %v", file, err)
			} else {
				prismaSchema = schema
			}
		}
	}

	// Build the dependency graph.
	graph, err := buildGraphWrapper(depTree, pkgDeps, mainLanguage, prismaSchema)
	if err != nil {
		log.Fatalf("Error building graph: %v", err)
	}

	// Save the dependency graph as JSON.
	err = SaveGraphData("graphdata.json", graph)
	if err != nil {
		log.Fatalf("Error writing graph data: %v", err)
	}
	fmt.Println("Graph data saved to graphdata.json")

	// Save the aggregated analysis reports.
	if jsDetected && len(jsReports) > 0 {
		err = SaveJSAnalysisReports(jsReports, "analysis_data.json")
		if err != nil {
			log.Fatalf("Error writing JS analysis data: %v", err)
		}
		fmt.Println("JS analysis data saved to analysis_data.json")
	}
	if len(goReports) > 0 {
		err = SaveGoAnalysisReports(goReports, "analysis_data.json")
		if err != nil {
			log.Fatalf("Error writing Go analysis data: %v", err)
		}
		fmt.Println("Go analysis data saved to analysis_data.json")
	}
	if len(pyReports) > 0 {
		err = SavePyAnalysisReports(pyReports, "analysis_data.json")
		if err != nil {
			log.Fatalf("Error writing Python analysis data: %v", err)
		}
		fmt.Println("Python analysis data saved to analysis_data.json")
	}
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
	} else if filepath.Base(file) == "schema.prisma" {
		return "prisma"
	}
	return "unknown"
}

// buildGraphWrapper should remain as a thin wrapper to call BuildGraph from graph_builder.go.
func buildGraphWrapper(depTree map[string][]string, pkgDeps map[string]bool, mainLang string, prismaSchema *PrismaSchema) (GraphData, error) {
	return BuildGraph(depTree, pkgDeps, mainLang, prismaSchema)
}
