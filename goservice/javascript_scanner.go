package main

import (
    "os"
    "regexp"
    "strings"
)

// ScanJSScanner scans a JavaScript/TypeScript/JSX/TSX file for dependency strings.
func ScanJSScanner(filePath string) ([]string, error) {
    content, err := os.ReadFile(filePath)
    if err != nil {
        return nil, err
    }
    text := string(content)

    // Match import statements.
	reImportFrom := regexp.MustCompile(`(?m)import\s+.*?\s+from\s+['"]([^'"]+)['"]`)
	reImportType := regexp.MustCompile(`(?m)import\s+type.*?\s+from\s+['"]([^'"]+)['"]`)
	reImportOnly := regexp.MustCompile(`(?m)import\s+['"]([^'"]+)['"]`)
	reRequire := regexp.MustCompile(`(?m)require\(\s*['"]([^'"]+)['"]\s*\)`)
	// Also capture static asset references like mp3, png, etc.
	reAsset := regexp.MustCompile(`(?m)['"]([^'"]+\.(?:mp3|png|jpg|jpeg|svg|gif))['"]`)
	
    depMap := make(map[string]bool)
    extractDep := func(matches [][]string) {
        for _, m := range matches {
            dep := m[1]
            // Skip relative imports.
            if !strings.HasPrefix(dep, ".") {
                depMap[dep] = true
            }
        }
    }

    extractDep(reImportFrom.FindAllStringSubmatch(text, -1))
    extractDep(reImportOnly.FindAllStringSubmatch(text, -1))
    extractDep(reRequire.FindAllStringSubmatch(text, -1))
    extractDep(reAsset.FindAllStringSubmatch(text, -1))
	extractDep(reImportType.FindAllStringSubmatch(text, -1))

    var deps []string
    for dep := range depMap {
        deps = append(deps, dep)
    }
    return deps, nil
}