PYTHON = python

test: install
	$(PYTHON) -m unittest

install: build
	$(PYTHON) setup.py develop

build: Makefile module.c third-party/quickjs.c third-party/quickjs.h
	$(PYTHON) setup.py build

format:
	$(PYTHON) -m yapf -i -r --style .style.yapf *.py
	clang-format-7 -i module.c

distribute: test
	rm -rf dist/
	$(PYTHON) setup.py sdist

upload-test: distribute
	$(PYTHON) -m twine upload --repository-url https://test.pypi.org/legacy/ dist/*

upload: distribute
	$(PYTHON) -m twine upload dist/*

clean:
	rm -rf build/ dist/ 
