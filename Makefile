
test: install
	poetry run python -X dev -m unittest

install: build
	poetry run python setup.py develop

build: Makefile module.c upstream-quickjs/quickjs.c upstream-quickjs/quickjs.h
	poetry run python setup.py build

format:
	poetry run python -m yapf -i -r --style .style.yapf *.py
	clang-format-7 -i module.c

distribute: test
	rm -rf dist/
	@echo "Now build the wheel for Windows in the dist/ folder."
	@echo "       poetry run python setup.py build -c mingw32"
	@echo "       poetry run python setup.py bdist_wheel --skip-build"
	@echo "Press enter to continue when done..."
	@read _
	poetry run python setup.py sdist

upload-test: distribute
	poetry run python -m twine upload --repository-url https://test.pypi.org/legacy/ dist/*

upload: distribute
	poetry run python -m twine upload dist/*

clean:
	rm -rf build/ dist/ 
	rm -f *.so
	rm -f *.pyd
