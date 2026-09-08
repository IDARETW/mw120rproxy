"""Local-only paths. Configure environment variables before starting Python."""
import os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
COD4=Path(os.environ.get('MW120R_COD4',str(ROOT/'external/CoD4')))
GAME=Path(os.environ.get('MW120R_GAME',str(ROOT/'external/Replay')))
REPLAY=GAME/'game_dx12_ship_replay.exe'
UNLINKER=Path(os.environ.get('MW120R_UNLINKER',str(ROOT/'external/OpenAssetTools/build/bin/Release_x86/Unlinker.exe')))
