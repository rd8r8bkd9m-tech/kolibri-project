#!/usr/bin/env python3
"""
Kolibri OS Architecture Linter

Analyzes project structure and provides recommendations for improving
code organization and reducing fragmentation.
"""

import os
import json
from pathlib import Path
from collections import defaultdict
from typing import Dict, List, Tuple, Any


class ArchitectureLinter:
    """Analyze and lint project architecture."""
    
    def __init__(self, root_dir: str = "."):
        self.root_dir = Path(root_dir)
        self.issues: List[Dict[str, Any]] = []
        self.stats: Dict[str, Any] = {}
    
    def analyze_directory_structure(self) -> Dict[str, int]:
        """Analyze directory structure and file distribution."""
        dir_counts = defaultdict(int)
        file_types = defaultdict(int)
        
        for path in self.root_dir.rglob("*"):
            if path.is_file() and not any(p.startswith('.') for p in path.parts):
                # Count by top-level directory
                try:
                    rel_path = path.relative_to(self.root_dir)
                    if len(rel_path.parts) > 1:
                        top_dir = rel_path.parts[0]
                        dir_counts[top_dir] += 1
                    
                    # Count by file extension
                    ext = path.suffix.lower() or "no_extension"
                    file_types[ext] += 1
                except ValueError:
                    continue
        
        self.stats["directory_distribution"] = dict(dir_counts)
        self.stats["file_types"] = dict(file_types)
        return self.stats
    
    def check_documentation_fragmentation(self) -> List[Dict[str, Any]]:
        """Check for documentation fragmentation."""
        issues = []
        md_files = list(self.root_dir.rglob("*.md"))
        
        # Group by directory
        doc_dirs = defaultdict(list)
        for md_file in md_files:
            try:
                rel_path = md_file.relative_to(self.root_dir)
                doc_dir = str(rel_path.parent)
                doc_dirs[doc_dir].append(md_file)
            except ValueError:
                continue
        
        # Check for excessive fragmentation
        total_md = len(md_files)
        num_dirs = len(doc_dirs)
        
        if total_md > 200:
            issues.append({
                "severity": "warning",
                "type": "documentation_fragmentation",
                "message": f"High documentation fragmentation: {total_md} markdown files in {num_dirs} directories",
                "recommendation": "Consider consolidating related documentation into fewer, more comprehensive files"
            })
        
        # Check for orphaned documentation
        for doc_dir, files in doc_dirs.items():
            if len(files) == 1 and 'README' not in files[0].name:
                issues.append({
                    "severity": "info",
                    "type": "orphaned_documentation",
                    "location": str(files[0]),
                    "message": f"Single documentation file in {doc_dir}",
                    "recommendation": "Consider merging with parent README or creating more content"
                })
        
        return issues
    
    def check_code_duplication(self) -> List[Dict[str, Any]]:
        """Check for potential code duplication."""
        issues = []
        
        # Find duplicate filenames (excluding common ones)
        filename_counts = defaultdict(list)
        excluded_names = {'__init__.py', 'Makefile', 'README.md', '.gitignore', 
                         'CMakeLists.txt', 'main.c', 'main.cpp'}
        
        for path in self.root_dir.rglob("*"):
            if path.is_file() and path.name not in excluded_names:
                try:
                    rel_path = path.relative_to(self.root_dir)
                    if not any(p.startswith('.') for p in rel_path.parts):
                        filename_counts[path.name].append(str(path))
                except ValueError:
                    continue
        
        # Report duplicates
        for name, locations in filename_counts.items():
            if len(locations) > 3:  # More than 3 files with same name
                issues.append({
                    "severity": "warning",
                    "type": "filename_duplication",
                    "filename": name,
                    "count": len(locations),
                    "locations": locations[:5],  # Show first 5
                    "message": f"{len(locations)} files named '{name}'",
                    "recommendation": "Consider using more descriptive names or organizing into subdirectories"
                })
        
        return issues
    
    def check_import_organization(self) -> List[Dict[str, Any]]:
        """Check Python import organization."""
        issues = []
        python_files = list(self.root_dir.rglob("*.py"))
        
        for py_file in python_files[:50]:  # Limit to first 50 files
            try:
                with open(py_file, 'r', encoding='utf-8', errors='ignore') as f:
                    lines = f.readlines()
                
                imports = []
                for i, line in enumerate(lines[:50], 1):  # Check first 50 lines
                    if line.strip().startswith(('import ', 'from ')):
                        imports.append((i, line.strip()))
                
                # Check for unordered imports
                if len(imports) > 5:
                    import_lines = [imp[1] for imp in imports]
                    if import_lines != sorted(import_lines):
                        issues.append({
                            "severity": "info",
                            "type": "unordered_imports",
                            "location": str(py_file),
                            "message": "Imports are not alphabetically ordered",
                            "recommendation": "Run 'isort' to organize imports"
                        })
                
                # Check for unused imports (basic check)
                if len(imports) > 10:
                    issues.append({
                        "severity": "info",
                        "type": "many_imports",
                        "location": str(py_file),
                        "message": f"File has {len(imports)} import statements",
                        "recommendation": "Consider splitting file or reviewing dependencies"
                    })
                    
            except Exception:
                continue
        
        return issues
    
    def check_circular_dependencies(self) -> List[Dict[str, Any]]:
        """Check for potential circular dependencies in Python modules."""
        issues = []
        
        # Build import graph for Python files in src/
        src_dir = self.root_dir / "src"
        if not src_dir.exists():
            return issues
        
        import_graph = defaultdict(set)
        py_files = list(src_dir.rglob("*.py"))
        
        for py_file in py_files:
            try:
                rel_path = py_file.relative_to(src_dir)
                module_name = str(rel_path.with_suffix('')).replace('/', '.')
                
                with open(py_file, 'r', encoding='utf-8', errors='ignore') as f:
                    content = f.read()
                
                # Find imports from same package
                for line in content.split('\n'):
                    if line.strip().startswith('from .') or line.strip().startswith(f'from src.'):
                        # Extract imported module
                        parts = line.split()
                        if len(parts) > 1:
                            imported = parts[1].strip()
                            if imported.startswith('.'):
                                # Relative import
                                imported = module_name.rsplit('.', 1)[0] + imported
                            import_graph[module_name].add(imported)
            except Exception:
                continue
        
        # Simple cycle detection (DFS)
        def has_cycle(node, visited, rec_stack):
            visited.add(node)
            rec_stack.add(node)
            
            for neighbor in import_graph.get(node, []):
                if neighbor not in visited:
                    if has_cycle(neighbor, visited, rec_stack):
                        return True
                elif neighbor in rec_stack:
                    return True
            
            rec_stack.remove(node)
            return False
        
        visited = set()
        for node in import_graph:
            if node not in visited:
                if has_cycle(node, visited, set()):
                    issues.append({
                        "severity": "error",
                        "type": "circular_dependency",
                        "module": node,
                        "message": f"Potential circular dependency detected involving {node}",
                        "recommendation": "Refactor to break circular dependency"
                    })
        
        return issues
    
    def generate_report(self) -> Dict[str, Any]:
        """Generate comprehensive architecture report."""
        print("Analyzing directory structure...")
        self.analyze_directory_structure()
        
        print("Checking documentation fragmentation...")
        doc_issues = self.check_documentation_fragmentation()
        self.issues.extend(doc_issues)
        
        print("Checking code duplication...")
        dup_issues = self.check_code_duplication()
        self.issues.extend(dup_issues)
        
        print("Checking import organization...")
        import_issues = self.check_import_organization()
        self.issues.extend(import_issues)
        
        print("Checking circular dependencies...")
        circular_issues = self.check_circular_dependencies()
        self.issues.extend(circular_issues)
        
        # Generate summary
        report = {
            "timestamp": Path.cwd(),
            "statistics": self.stats,
            "issues": self.issues,
            "summary": {
                "total_issues": len(self.issues),
                "errors": len([i for i in self.issues if i.get("severity") == "error"]),
                "warnings": len([i for i in self.issues if i.get("severity") == "warning"]),
                "info": len([i for i in self.issues if i.get("severity") == "info"])
            }
        }
        
        # Save report
        report_path = Path("architecture_report.json")
        with open(report_path, 'w') as f:
            json.dump(report, f, indent=2, default=str)
        
        # Print summary
        print(f"\n{'='*60}")
        print("ARCHITECTURE ANALYSIS SUMMARY")
        print(f"{'='*60}")
        print(f"Total Issues: {report['summary']['total_issues']}")
        print(f"  Errors:   {report['summary']['errors']}")
        print(f"  Warnings: {report['summary']['warnings']}")
        print(f"  Info:     {report['summary']['info']}")
        print(f"\nFull report saved to: {report_path}")
        
        return report


def main():
    """Main entry point."""
    import argparse
    
    parser = argparse.ArgumentParser(description="Kolibri OS Architecture Linter")
    parser.add_argument("--root", default=".", help="Root directory to analyze")
    parser.add_argument("--output", help="Output file for JSON report")
    args = parser.parse_args()
    
    linter = ArchitectureLinter(root_dir=args.root)
    report = linter.generate_report()
    
    if args.output:
        with open(args.output, 'w') as f:
            json.dump(report, f, indent=2, default=str)


if __name__ == "__main__":
    main()
