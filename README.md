## Clone the repository:
```bash
	git clone https://github.com/NiktWazny-PL/NwCompile---C-C-GXX-Compilation-Tool
```

## Build The Tool Using g++ (no support for other compilers yet, sorry)
``` bash
	g++ Tool/Source/main.cpp Tool/Source/Tool.cpp Tool/Binary/Yaml.obj -O3 -std=c++23 -o Tool/App.exe
```

## How to run the tool
#### 1. Make sure you're in the root directory(the one with the README.md)
#### 2. Make sure your Projects.yaml is set up properly with all your projects
#### 3. Run command:
```bash
	Tool/app.exe [FILE WITH THE PROJECTS].yaml
```
#### 4. Watch as the tool compiles your code
