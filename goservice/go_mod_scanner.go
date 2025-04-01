package main

import (
    "bufio"
    "os"
    "regexp"
    "strings"
)

// ScanGoMod reads a go.mod file and returns a list of module dependencies.
func ScanGoMod(modFilePath string) ([]string, error) {
    file, err := os.Open(modFilePath)
    if err != nil {
        return nil, err
    }
    defer file.Close()

    depMap := make(map[string]bool)
    scanner := bufio.NewScanner(file)

    // Regular expressions to match require lines.
    requireRegex := regexp.MustCompile(`^require\s+(?:\()?([^ \t\n]+)`)
    blockLineRegex := regexp.MustCompile(`^([^ \t\n]+)\s+`)

    inRequireBlock := false
    for scanner.Scan() {
        line := strings.TrimSpace(scanner.Text())
        if strings.HasPrefix(line, "require (") {
            inRequireBlock = true
            continue
        }
        if inRequireBlock {
            if line == ")" {
                inRequireBlock = false
                continue
            }
            if matches := blockLineRegex.FindStringSubmatch(line); len(matches) > 1 {
                depMap[matches[1]] = true
            }
        } else {
            if strings.HasPrefix(line, "require") {
                if matches := requireRegex.FindStringSubmatch(line); len(matches) > 1 {
                    depMap[matches[1]] = true
                }
            }
        }
    }

    if err := scanner.Err(); err != nil {
        return nil, err
    }

    var deps []string
    for dep := range depMap {
        deps = append(deps, dep)
    }
    return deps, nil
}