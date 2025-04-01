package main

import (
    "os"
    "regexp"
)

// ScanPythonScanner scans a Python file for dependency strings (imports).
func ScanPythonScanner(filePath string) ([]string, error) {
    content, err := os.ReadFile(filePath)
    if err != nil {
        return nil, err
	}
    text := string(content)

    // Match patterns like "import module" and "from module import ..."
    reImport := regexp.MustCompile(`(?m)^(?:import|from)\s+([\w\.]+)`)
    depMap := make(map[string]bool)
    for _, m := range reImport.FindAllStringSubmatch(text, -1) {
        depMap[m[1]] = true
    }
    var deps []string
    for dep := range depMap {
        deps = append(deps, dep)
    }
    return deps, nil
}