package main

import (
	"io/fs"
	"path/filepath"
	"strings"
)

// Map of languages to their file extensions
var langExtensions = map[string][]string{
	"javascript": {".js", ".jsx", ".ts", ".tsx"},
	"python":     {".py"},
	"go":         {".go"},
}

// FindEntryPoints walks through the repository and returns files that match known extensions.
func FindEntryPoints(root string, languages []string) ([]string, error) {
	var entryPoints []string

	// Build a flat list of extensions to search for.
	var exts []string
	for _, lang := range languages {
		if langExt, ok := langExtensions[lang]; ok {
			exts = append(exts, langExt...)
		}
	}

	err := filepath.WalkDir(root, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if !d.IsDir() {
			for _, ext := range exts {
				if strings.HasSuffix(path, ext) {
					entryPoints = append(entryPoints, path)
					break
				}
			}
		}
		return nil
	})
	return entryPoints, err
}

// DetectLanguages scans the repository and returns a list of detected languages.
func DetectLanguages(root string) ([]string, error) {
	langSet := make(map[string]bool)
	err := filepath.WalkDir(root, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if !d.IsDir() {
			if strings.HasSuffix(path, ".js") || strings.HasSuffix(path, ".ts") ||
				strings.HasSuffix(path, ".jsx") || strings.HasSuffix(path, ".tsx") {
				langSet["javascript"] = true
			} else if strings.HasSuffix(path, ".py") {
				langSet["python"] = true
			} else if strings.HasSuffix(path, ".go") {
				langSet["go"] = true
			}
		}
		return nil
	})
	if err != nil {
		return nil, err
	}
	var languages []string
	for lang := range langSet {
		languages = append(languages, lang)
	}
	return languages, nil
}
