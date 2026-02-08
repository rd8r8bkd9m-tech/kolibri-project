#!/usr/bin/env python3
"""
Kolibri Neuroscope - Real-time Observation Instrument
Инструмент наблюдения за состоянием нейронного кластера Kolibri.
"""

import time
import psutil
import os
from datetime import datetime
from rich.live import Live
from rich.layout import Layout
from rich.panel import Panel
from rich.table import Table
from rich.text import Text
from rich.console import Console
from rich import box

GENOME_PATH = "build/training/auto_genome.dat"
LOG_PATH = "logs/knowledge_server.log"

def get_header():
    grid = Table.grid(expand=True)
    grid.add_column(justify="center", ratio=1)
    grid.add_column(justify="right")
    grid.add_row(
        "[b cyan]Kolibri OS[/] Neuroscope v1.0",
        datetime.now().strftime("%H:%M:%S")
    )
    return Panel(grid, style="white on blue")

def get_system_stats():
    cpu = psutil.cpu_percent()
    mem = psutil.virtual_memory()
    table = Table(box=None, expand=True)
    table.add_column("Metric", style="cyan")
    table.add_column("Value", justify="right", style="green")
    
    table.add_row("CPU Load", f"{cpu}%")
    table.add_row("RAM Usage", f"{mem.percent}%")
    table.add_row("RAM Used", f"{mem.used / 1024**3:.1f} GB")
    table.add_row("Uptime", f"{(time.time() - psutil.boot_time()) / 3600:.1f} h")
    
    return Panel(table, title="[System Vitality]", border_style="green")

def get_nodes():
    table = Table(box=box.SIMPLE, expand=True)
    table.add_column("PID", style="magenta")
    table.add_column("Name", style="yellow")
    table.add_column("Status", style="green")
    
    found = False
    for proc in psutil.process_iter(['pid', 'name', 'cmdline']):
        try:
            if 'kolibri_node' in (proc.info['name'] or ""):
                found = True
                table.add_row(
                    str(proc.info['pid']),
                    "Kolibri Node (C23)",
                    "RUNNING"
                )
            elif 'knowledge_server' in (proc.info['name'] or ""):
                found = True
                table.add_row(
                    str(proc.info['pid']),
                    "Knowledge Server",
                    "ACTIVE"
                )
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
            
    if not found:
        table.add_row("-", "No active nodes", "[red]OFFLINE[/]")
        
    return Panel(table, title="[Neural Swarm]", border_style="magenta")

def read_genome_tail():
    if not os.path.exists(GENOME_PATH):
        return Panel(Text("Genome file not found", justify="center", style="red"), title="[Genome Stream]")
    
    try:
        size = os.path.getsize(GENOME_PATH)
        BLOCK_SIZE = 608 # Approximation of ReasonBlock size
        
        with open(GENOME_PATH, "rb") as f:
            if size > BLOCK_SIZE * 5:
                f.seek(-BLOCK_SIZE * 5, 2)
            data = f.read()
            
        # Hex view
        text = Text()
        for i, byte in enumerate(data):
            color = "green" if byte > 128 else "bright_black"
            if i % 32 == 0 and i > 0:
                text.append("\n")
            text.append(f"{byte:02X} ", style=color)
            
        return Panel(text, title=f"[Genome Stream] Size: {size} bytes", border_style="cyan")
    except Exception as e:
        return Panel(Text(f"Error reading genome: {e}", style="red"), title="[Genome Stream]")

def make_layout():
    layout = Layout()
    layout.split(
        Layout(name="header", size=3),
        Layout(name="main", ratio=1),
    )
    layout["main"].split_row(
        Layout(name="left"),
        Layout(name="right", ratio=2),
    )
    layout["left"].split(
        Layout(name="stats"),
        Layout(name="nodes"),
    )
    return layout

def update(layout):
    layout["header"].update(get_header())
    layout["stats"].update(get_system_stats())
    layout["nodes"].update(get_nodes())
    layout["right"].update(read_genome_tail())

if __name__ == "__main__":
    console = Console()
    layout = make_layout()
    
    console.print("[yellow]Initializing Neuroscope...[/]")
    time.sleep(1)
    
    try:
        with Live(layout, refresh_per_second=2, screen=True):
            while True:
                update(layout)
                time.sleep(0.5)
    except KeyboardInterrupt:
        console.print("[red]Neuroscope terminated.[/]")
