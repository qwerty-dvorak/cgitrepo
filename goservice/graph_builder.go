package main

import (
	"encoding/json"
	"os"
	"path/filepath"
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
	Nodes []Node `json:"nodes"`
	Links []Link `json:"links"`
}

// BuildGraph constructs a graph from the dependency tree and package dependencies.
func BuildGraph(depTree map[string][]string, pkgDeps map[string]bool) (GraphData, error) {
	var graph GraphData
	fileNodeIDs := make(map[string]int)
	depNodeIDs := make(map[string]int)
	idCounter := 0

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

	return graph, nil
}

// SaveGraphData writes the graph JSON to a file.
func SaveGraphData(filename string, graph GraphData) error {
	jsonData, err := json.MarshalIndent(graph, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(filename, jsonData, 0644)
}
