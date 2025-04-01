package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
)

// Node represents a node in the dependency graph.
type Node struct {
	ID           int      `json:"id"`
	Name         string   `json:"name"`
	Category     string   `json:"category"`
	Path         string   `json:"path,omitempty"`
	Color        string   `json:"color,omitempty"`
	Content      string   `json:"content,omitempty"`
	Dependencies []string `json:"dependencies,omitempty"`
}

// Link represents an edge between two nodes.
type Link struct {
	Source   int     `json:"source"`
	Target   int     `json:"target"`
	Relation string  `json:"relation"`
	Strength float64 `json:"strength,omitempty"`
}

// GraphData holds all nodes and links.
type GraphData struct {
	Nodes    []Node `json:"nodes"`
	Links    []Link `json:"links"`
	Language string `json:"language"`
}


// BuildGraph constructs a graph from the dependency tree, package dependencies, and Prisma schema.
func BuildGraph(depTree map[string][]string, pkgDeps map[string]bool, mainLanguage string, prismaSchema *PrismaSchema) (GraphData, error) {
	var graph GraphData
	fileNodeIDs := make(map[string]int)
	depNodeIDs := make(map[string]int)
	idCounter := 0

	// Set the primary language
	graph.Language = mainLanguage

	// Create a node for every file.
	for file, deps := range depTree {
		// Read file content
		content, err := os.ReadFile(file)
		var fileContent string
		if err == nil {
			fileContent = string(content)
		}

		node := Node{
			ID:           idCounter,
			Name:         filepath.Base(file),
			Category:     "file",
			Path:         file,
			Content:      fileContent,
			Dependencies: deps,
		}
		graph.Nodes = append(graph.Nodes, node)
		fileNodeIDs[file] = idCounter
		idCounter++
	}

	// Create nodes for dependencies.
	for _, deps := range depTree {
		for _, dep := range deps {
			if _, exists := depNodeIDs[dep]; !exists {
				category := "dependency"
				if pkgDeps != nil && pkgDeps[dep] {
					category = "library"
				}
				node := Node{
					ID:       idCounter,
					Name:     dep,
					Category: category,
				}
				graph.Nodes = append(graph.Nodes, node)
				depNodeIDs[dep] = idCounter
				idCounter++
			}
		}
	}

	// Create links from file nodes to dependency nodes.
	for file, deps := range depTree {
		srcID := fileNodeIDs[file]
		for _, dep := range deps {
			targetID, ok := depNodeIDs[dep]
			if !ok {
				continue
			}
			link := Link{
				Source:   srcID,
				Target:   targetID,
				Relation: "depends",
			}
			graph.Links = append(graph.Links, link)
		}
	}

	// Add Prisma schema information if available
	if prismaSchema != nil {
		// Create a node for the schema file itself
		schemaNodeID := idCounter
		schemaNode := Node{
			ID:       schemaNodeID,
			Name:     filepath.Base(prismaSchema.FilePath),
			Category: "prisma_schema",
			Path:     prismaSchema.FilePath,
			Color:    "#5a67d8", // Use a distinctive color for Prisma
		}
		graph.Nodes = append(graph.Nodes, schemaNode)
		idCounter++

		// Create nodes for each model in the schema
		for _, model := range prismaSchema.Models {
			modelNodeID := idCounter
			modelNode := Node{
				ID:       modelNodeID,
				Name:     model.Name,
				Category: "prisma_model",
				Color:    "#4c51bf",
				Content:  formatModelFields(model.Fields),
			}
			graph.Nodes = append(graph.Nodes, modelNode)
			idCounter++

			// Link schema to model
			schemaToModelLink := Link{
				Source:   schemaNodeID,
				Target:   modelNodeID,
				Relation: "defines",
				Strength: 1.0,
			}
			graph.Links = append(graph.Links, schemaToModelLink)
		}
	}

	return graph, nil
}

// Helper function to format model fields for display
func formatModelFields(fields map[string]string) string {
	if len(fields) == 0 {
		return ""
	}

	var result strings.Builder
	for name, fieldType := range fields {
		result.WriteString(name)
		result.WriteString(": ")
		result.WriteString(fieldType)
		result.WriteString("\n")
	}
	return result.String()
}

// SaveGraphData writes the graph JSON to a file.
func SaveGraphData(filename string, graph GraphData) error {
	jsonData, err := json.MarshalIndent(graph, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(filename, jsonData, 0644)
}
