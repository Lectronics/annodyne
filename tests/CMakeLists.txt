# Test executables are not layered targets: they may link anything, including core/io (ADR-0001).
# Folders and labels: unit, contract, audio, golden, torture (ADR-0004).

add_subdirectory(unit)
add_subdirectory(contract)
