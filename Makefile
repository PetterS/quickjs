
test: install
	pipenv run python -m unittest

install: build
	pipenv run python setup.py develop

build: Makefile module.c third-party/quickjs.c third-party/quickjs.h
	pipenv run python setup.py build

format:
	pipenv run python -m yapf -i -r --style .style.yapf *.py
	clang-format-7 -i module.c

distribute: test
	rm -rf dist/
	@echo "Now build the wheel for Windows in the dist/ folder."
	@echo "       pipenv run python setup.py build -c mingw32"
	@echo "       pipenv run python setup.py bdist_wheel --skip-build"
	@echo "Press enter to continue when done..."
	@read _
	pipenv run python setup.py sdist

upload-test: distribute
	pipenv run python -m twine upload --repository-url https://test.pypi.org/legacy/ dist/*

upload: distribute
	pipenv run python -m twine upload dist/*

clean:
	rm -rf build/ dist/ 
	rm *.so
	rm *.pyd
