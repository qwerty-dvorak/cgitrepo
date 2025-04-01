package main

import (
	"encoding/json"
	"os"
	"path/filepath"
)

type PackageJSON struct {
	Dependencies    map[string]string `json:"dependencies"`
	DevDependencies map[string]string `json:"devDependencies"`
}

// ReadPackageJSON reads and parses package.json and returns a map of dependency names.
func ReadPackageJSON(repoDir string) (map[string]bool, error) {
	pkgPath := filepath.Join(repoDir, "package.json")
	content, err := os.ReadFile(pkgPath)
	if err != nil {
		return nil, err
	}
	var pkg PackageJSON
	err = json.Unmarshal(content, &pkg)
	if err != nil {
		return nil, err
	}

	deps := make(map[string]bool)
	for dep := range pkg.Dependencies {
		deps[dep] = true
	}
	for dep := range pkg.DevDependencies {
		deps[dep] = true
	}

	return deps, nil
}
