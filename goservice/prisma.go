package main

import (
	"bufio"
	"os"
	"strings"
)

// PrismaModel represents a model in the Prisma schema
type PrismaModel struct {
	Name      string            `json:"name"`
	Fields    map[string]string `json:"fields"` // Field name -> type
	Relations []struct {
		Field        string `json:"field"`
		RelatedModel string `json:"relatedModel"`
		Type         string `json:"type"` // e.g., "one-to-many", "many-to-one"
	} `json:"relations"`
}

// PrismaSchema represents the entire schema structure
type PrismaSchema struct {
	FilePath string        `json:"filePath"`
	Models   []PrismaModel `json:"models"`
}

// ParsePrismaSchema reads a Prisma schema file and extracts model information
func ParsePrismaSchema(filePath string) (*PrismaSchema, error) {
	file, err := os.Open(filePath)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	schema := &PrismaSchema{
		FilePath: filePath,
		Models:   []PrismaModel{},
	}

	scanner := bufio.NewScanner(file)
	var currentModel *PrismaModel
	inModel := false

	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())

		// Skip empty lines and comments
		if line == "" || strings.HasPrefix(line, "//") {
			continue
		}

		// Check for model definition
		if strings.HasPrefix(line, "model ") {
			inModel = true
			modelName := strings.TrimPrefix(line, "model ")
			modelName = strings.TrimSuffix(modelName, " {")
			currentModel = &PrismaModel{
				Name:   modelName,
				Fields: make(map[string]string),
			}
			continue
		}

		// Check for end of model
		if inModel && line == "}" {
			schema.Models = append(schema.Models, *currentModel)
			inModel = false
			currentModel = nil
			continue
		}

		// Parse field definitions within a model
		if inModel && currentModel != nil {
			// Skip relation fields or other complex definitions for simplicity
			if !strings.Contains(line, "//") && strings.Contains(line, " ") {
				parts := strings.SplitN(line, " ", 2)
				if len(parts) == 2 {
					fieldName := parts[0]
					fieldType := strings.Split(parts[1], " ")[0] // Get just the type without attributes
					currentModel.Fields[fieldName] = fieldType
				}
			}

			// While parsing fields, also look for relations
			if strings.Contains(line, "@relation") {
				// Extract relation information
				fieldName := strings.Split(line, " ")[0]
				relatedModel := ""

				// Extract the related model name - simplistic parsing
				if strings.Contains(line, "references: [") {
					parts := strings.Split(line, "references: [")
					if len(parts) > 1 {
						refParts := strings.Split(parts[1], "]")
						if len(refParts) > 0 {
							relatedModel = strings.TrimSpace(refParts[0])
						}
					}
				}

				// Add this relation to the model
				if relatedModel != "" {
					relation := struct {
						Field        string `json:"field"`
						RelatedModel string `json:"relatedModel"`
						Type         string `json:"type"`
					}{
						Field:        fieldName,
						RelatedModel: relatedModel,
						Type:         "relation", // A more sophisticated parser would determine the exact type
					}

					// Append to Relations slice
					currentModel.Relations = append(currentModel.Relations, relation)
				}
			}
		}
	}

	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return schema, nil
}
