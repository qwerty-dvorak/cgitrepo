package main

import (
    "os"
    "regexp"
)

// ScanGoScanner scans a Go file for import statements with improved parsing.
func ScanGoScanner(filePath string) ([]string, error) {
    content, err := os.ReadFile(filePath)
    if err != nil {
        return nil, err
    }
    text := string(content)
    depMap := make(map[string]bool)

    // Pattern for single-line imports: import "package" or alias "package"
    singleImportRegex := regexp.MustCompile(`import\s+(?:\S+\s+)?\"([^\"]+)\"`)
    matches1 := singleImportRegex.FindAllStringSubmatch(text, -1)
    for _, match := range matches1 {
        if match[1] != "" {
            depMap[match[1]] = true
        }
    }

    // Pattern for grouped imports: import ( ... )
    groupImportRegex := regexp.MustCompile(`import\s+\(([\s\S]*?)\)`)
    matches2 := groupImportRegex.FindAllStringSubmatch(text, -1)
    for _, m := range matches2 {
        groupText := m[1]
        // Each line may have an alias and a quoted package.
        lineRegex := regexp.MustCompile(`(?:\S+\s+)?\"([^\"]+)\"`)
        lineMatches := lineRegex.FindAllStringSubmatch(groupText, -1)
        for _, lm := range lineMatches {
            if lm[1] != "" {
                depMap[lm[1]] = true
            }
        }
    }

    var deps []string
    for dep := range depMap {
        deps = append(deps, dep)
    }
    return deps, nil
}