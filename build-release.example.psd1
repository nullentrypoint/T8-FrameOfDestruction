@{
    # Relative paths are resolved from this configuration file's directory.
    OutputDirectory = 'artifacts'
    # Optional TEKKEN 8 installation root or Polaris\Binaries\Win64 directory.
    # Leave empty to only create the ZIP. Installation overwrites packaged files.
    GameDirectory = ''
    MemoryAddressPath = 'config\memory_address.ini'
    ActionableFramesPath = 'config\actionable_frames.json'
    ReadmePath = 'packaging\readme.txt'

    # Leave empty to download the x64 ASI loader automatically.
    Dinput8Path = ''
    LoaderUrl = 'https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v10.0.2/Ultimate-ASI-Loader-NoPDB_x64.zip'
    LoaderSha256 = 'FE964ADF4DE7F467FF39C440A4059DE02E37D0574A62944EEF2F55A35EDAFA2F'
}
