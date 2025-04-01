package main

import (
	"errors"
	"path/filepath"
)

// ScanDependencies chooses the appropriate scanner based on the file extension.
func ScanDependencies(filePath string) ([]string, error) {
	// Skip scanning for Prisma schema files. They are handled separately.
	if filepath.Base(filePath) == "schema.prisma" {
		return []string{}, nil
	}

	if hasSuffix(filePath, ".js") ||
		hasSuffix(filePath, ".ts") ||
		hasSuffix(filePath, ".jsx") ||
		hasSuffix(filePath, ".tsx") {
		return ScanJSScanner(filePath)
	} else if hasSuffix(filePath, ".py") {
		return ScanPythonScanner(filePath)
	} else if hasSuffix(filePath, ".go") {
		return ScanGoScanner(filePath)
	}
	return nil, errors.New("unsupported file type for dependency scanning")
}

// hasSuffix is a helper function.
func hasSuffix(path, suffix string) bool {
	l := len(suffix)
	if len(path) < l {
		return false
	}
	return path[len(path)-l:] == suffix
}
