import pygame
import json
import threading
import time
import os
import math
from typing import List, Tuple, Set, Dict

# --- Configuration ---
MAP_FILE = "example_problems/custom_domain/maps/mymap.map"
JSON_FILE = "test.json"
RENDER_FPS = 60
# --- SPEED CONTROL ---
# Speed of the animation. 1.0 is real-time, 3.0 is three times as fast, etc.
SPEED_FACTOR = 3.0
AGENT_COLORS = [
    (31, 119, 180), (255, 127, 14), (44, 160, 44), (214, 39, 40),
    (148, 103, 189), (140, 86, 75), (227, 119, 194), (127, 127, 127),
    (188, 189, 34), (23, 190, 207), (255, 152, 150), (197, 176, 213)
]

class LiveMAPFVisualizer:
    """
    A live, time-accurate visualizer for MAPF where each action
    (move or rotate) consumes one discrete timestep.
    """

    def __init__(self, map_file: str, json_file: str):
        self.map_file = map_file
        self.json_file = json_file
        self.obstacles, self.nrows, self.ncols = self._parse_map()

        # Time and Animation State
        self.paused: bool = True
        self.current_timestep: float = 0.0
        self.makespan: int = 1
        self.animation_start_time: float = 0.0
        self.time_offset_on_pause: float = 0.0

        # Agent, Path, and Task Data
        self.agents_paths: List[List[Tuple[int, int, int]]] = []
        self.tasks: List[Dict] = []
        self.task_events: Dict[int, List[Dict]] = {}
        self.team_size: int = 0
        self.sum_of_cost: int = 0
        self.tasks_finished: int = 0
        self.last_json_mtime: float = 0
        self.running: bool = True

        pygame.init()
        self._setup_display()

        self.file_watcher_thread = threading.Thread(target=self._watch_json_file, daemon=True)
        self.file_watcher_thread.start()
        print("Visualizer initialized. Waiting for test.json to be created or updated...")

    def _setup_display(self):
        info = pygame.display.Info()
        screen_w, screen_h = info.current_w, info.current_h
        max_grid_h = int(screen_h * 0.9)
        legend_w = 300
        cell_size_h = max_grid_h // self.nrows if self.nrows > 0 else 60
        cell_size_w = (screen_w - legend_w) // self.ncols if self.ncols > 0 else 60
        self.cell_size = min(cell_size_h, cell_size_w, 60)

        self.margin = 30
        self.grid_w = self.ncols * self.cell_size
        self.grid_h = self.nrows * self.cell_size
        self.width = self.grid_w + self.margin * 2 + legend_w
        self.height = self.grid_h + self.margin * 2

        self.screen = pygame.display.set_mode((self.width, self.height))
        pygame.display.set_caption("MAPF Live Visualizer")

        self.font = pygame.font.SysFont('Arial', 20, bold=True)
        self.small_font = pygame.font.SysFont('Arial', 16)
        self.task_font = pygame.font.SysFont('Arial', 12, bold=True)
        self.clock = pygame.time.Clock()
        self.bg_color = (245, 245, 245)
        self.grid_color = (200, 200, 200)

        self._create_background_surface()

    def _create_background_surface(self):
        self.bg_surface = pygame.Surface((self.width, self.height))
        self.bg_surface.fill(self.bg_color)
        for r, c in self.obstacles:
            pygame.draw.rect(self.bg_surface, (50, 50, 50),
                             (self.margin + c * self.cell_size, self.margin + r * self.cell_size,
                              self.cell_size, self.cell_size))
        for x in range(self.ncols + 1):
            pygame.draw.line(self.bg_surface, self.grid_color,
                             (self.margin + x * self.cell_size, self.margin),
                             (self.margin + x * self.cell_size, self.margin + self.grid_h), 1)
        for y in range(self.nrows + 1):
            pygame.draw.line(self.bg_surface, self.grid_color,
                             (self.margin, self.margin + y * self.cell_size),
                             (self.margin + self.grid_w, self.margin + y * self.cell_size), 1)

    def _parse_map(self) -> Tuple[Set[Tuple[int, int]], int, int]:
        obstacles, nrows, ncols = set(), 0, 0
        try:
            with open(self.map_file, 'r') as f:
                lines, map_started, row_idx = f.readlines(), False, 0
                for line in lines:
                    if "height" in line: nrows = int(line.split()[1])
                    elif "width" in line: ncols = int(line.split()[1])
                    elif "map" in line: map_started = True
                    elif map_started and row_idx < nrows:
                        for col_idx, char in enumerate(line.strip()):
                            if char in ['@', 'T']: obstacles.add((row_idx, col_idx))
                        row_idx += 1
        except FileNotFoundError:
            print(f"Error: Map file not found at '{self.map_file}'")
        return obstacles, nrows, ncols

    def _calculate_path_from_actions(self, start_row: int, start_col: int, start_orientation: int, actions_str: str) -> List[Tuple[int, int, int]]:
        """Calculates a sequence of states where each action (move or rotate) is a distinct timestep."""
        row, col, orientation = start_row, start_col, start_orientation
        path = [(row, col, orientation)]
        actions = actions_str.split(',') if actions_str else []

        for action in actions:
            last_row, last_col, last_orientation = path[-1]
            if action == 'R':
                new_orientation = (last_orientation + 1) % 4
                path.append((last_col, last_row, new_orientation))
            elif action == 'C':
                new_orientation = (last_orientation - 1 + 4) % 4
                path.append((last_col, last_row, new_orientation))
            elif action == 'F':
                new_row, new_col = last_row, last_col
                if last_orientation == 0: new_row -= 1
                elif last_orientation == 1: new_col += 1
                elif last_orientation == 2: new_row += 1
                elif last_orientation == 3: new_col -= 1
                path.append((new_row, new_col, last_orientation))
            elif action == 'W':
                path.append(path[-1]) # Append the same state again for a wait action

        return path


    def _load_data_from_json(self):
        try:
            with open(self.json_file, 'r') as f:
                data = json.load(f)

            self.team_size = data.get("teamSize", 0)
            self.sum_of_cost = data.get("sumOfCost", 0)
            self.tasks_finished = data.get("numTaskFinished", 0)
            self.tasks = data.get("tasks", [])
            
            self.task_events.clear()
            for agent_id, agent_events in enumerate(data.get("events", [])):
                for (task_id, timestep, event_type) in agent_events:
                    if task_id not in self.task_events:
                        self.task_events[task_id] = []
                    self.task_events[task_id].append({
                        "agent_id": agent_id,
                        "timestep": timestep,
                        "type": event_type
                    })
            
            for task_id in self.task_events:
                self.task_events[task_id].sort(key=lambda e: e['timestep'])

            starts = data.get("start", [])
            actual_paths_actions = data.get("actualPaths", [])
            new_paths = []
            orientation_map = {'N': 0, 'E': 1, 'S': 2, 'W': 3}

            for i in range(self.team_size):
                if i < len(starts) and i < len(actual_paths_actions):
                    row, col, orientation_str = starts[i]
                    orientation = orientation_map.get(orientation_str, 0)
                    path = self._calculate_path_from_actions(row, col, orientation, actual_paths_actions[i])
                    new_paths.append(path)

            self.agents_paths = new_paths
            self.makespan = max(len(p) for p in self.agents_paths) if self.agents_paths else 1
            
            if self.animation_start_time == 0 and not self.paused:
                self.animation_start_time = time.time()

            print(f"Successfully reloaded '{self.json_file}'.")

        except (FileNotFoundError, json.JSONDecodeError, KeyError) as e:
            print(f"Waiting for valid data in '{self.json_file}'... Error: {e}")
            self.agents_paths, self.tasks, self.task_events = [], [], {}
            self.makespan = 1

    def _watch_json_file(self):
        while self.running:
            try:
                mtime = os.path.getmtime(self.json_file)
                if mtime != self.last_json_mtime:
                    self.last_json_mtime = mtime
                    pygame.event.post(pygame.event.Event(pygame.USEREVENT, {'handler': 'reload_data'}))
            except OSError:
                pass
            time.sleep(0.5)

    def _handle_events(self):
        for event in pygame.event.get():
            if event.type == pygame.QUIT: self.running = False
            elif event.type == pygame.USEREVENT and event.handler == 'reload_data': self._load_data_from_json()
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE: self.running = False
                elif event.key == pygame.K_SPACE: self.toggle_pause()
                elif event.key == pygame.K_RIGHT and self.paused:
                    self.current_timestep = min(self.current_timestep + 1, self.makespan - 1)
                    self.time_offset_on_pause = self.current_timestep
                elif event.key == pygame.K_LEFT and self.paused:
                    self.current_timestep = max(self.current_timestep - 1, 0)
                    self.time_offset_on_pause = self.current_timestep

    def toggle_pause(self):
        self.paused = not self.paused
        if self.paused:
            self.time_offset_on_pause = (time.time() - self.animation_start_time) * SPEED_FACTOR
        else:
            if self.animation_start_time == 0: self.animation_start_time = time.time() - self.current_timestep / SPEED_FACTOR
            else: self.animation_start_time = time.time() - self.time_offset_on_pause / SPEED_FACTOR

    def _update(self):
        if not self.paused:
            elapsed_time = time.time() - self.animation_start_time
            self.current_timestep = elapsed_time * SPEED_FACTOR

    def _draw(self):
        self.screen.blit(self.bg_surface, (0, 0))
        self._draw_tasks()
        self._draw_agents()
        self._draw_legend()
        pygame.display.flip()

    def get_task_state_at_timestep(self, task_id: int, timestep: int) -> Tuple[str, Dict]:
        last_event = None
        if task_id in self.task_events:
            for event in self.task_events[task_id]:
                if event['timestep'] <= timestep:
                    last_event = event
                else:
                    break
        if last_event: return last_event['type'], last_event
        return "unassigned", None

    def _draw_tasks(self):
        display_frame = int(self.current_timestep)
        for task in self.tasks:
            task_id, start_r, start_c, goal_r, goal_c = task
            state, _ = self.get_task_state_at_timestep(task_id, display_frame)
            if state == "assigned":
                start_pos_px = (self.margin + start_c * self.cell_size, self.margin + start_r * self.cell_size)
                pygame.draw.rect(self.screen, (34, 139, 34), (*start_pos_px, self.cell_size, self.cell_size), border_radius=3)
                goal_pos_px = (self.margin + goal_c * self.cell_size, self.margin + goal_r * self.cell_size)
                pygame.draw.rect(self.screen, (178, 34, 34), (*goal_pos_px, self.cell_size, self.cell_size), border_radius=3)
                id_text = self.task_font.render(str(task_id), True, (255, 255, 255))
                self.screen.blit(id_text, id_text.get_rect(center=(start_pos_px[0] + self.cell_size/2, start_pos_px[1] + self.cell_size/2)))
                self.screen.blit(id_text, id_text.get_rect(center=(goal_pos_px[0] + self.cell_size/2, goal_pos_px[1] + self.cell_size/2)))

    def _draw_agents(self):
        frame_idx = int(self.current_timestep)
        for i, path in enumerate(self.agents_paths):
            if not path or frame_idx >= len(path): continue
            
            color = AGENT_COLORS[i % len(AGENT_COLORS)]
            r, c, orientation = path[frame_idx]
            
            center_x = self.margin + c * self.cell_size + self.cell_size / 2
            center_y = self.margin + r * self.cell_size + self.cell_size / 2
            
            agent_radius = self.cell_size / 2 - 2
            pygame.draw.circle(self.screen, color, (center_x, center_y), agent_radius)
            
            angle_rad = math.radians({0: -90, 1: 0, 2: 90, 3: 180}.get(orientation, 0))
            dot_x = center_x + (agent_radius * 0.7) * math.cos(angle_rad)
            dot_y = center_y + (agent_radius * 0.7) * math.sin(angle_rad)
            pygame.draw.circle(self.screen, (0, 0, 0), (dot_x, dot_y), self.cell_size / 10)

            id_text = self.font.render(str(i), True, (255, 255, 255))
            self.screen.blit(id_text, id_text.get_rect(center=(center_x, center_y)))

    def _draw_legend(self):
        legend_x, y = self.grid_w + self.margin * 2 + 20, self.margin
        def draw_text(text, y_pos, font, color=(0,0,0)):
            surface = font.render(text, True, color)
            self.screen.blit(surface, (legend_x, y_pos))
            return y_pos + surface.get_height() + 5
        
        y = draw_text("Live MAPF Status", y, self.font)
        y += 15
        display_frame = int(self.current_timestep)
        
        tasks_finished_now = 0
        for task_id, _ in self.task_events.items():
             state, _ = self.get_task_state_at_timestep(task_id, display_frame)
             if state == "finished": tasks_finished_now += 1

        y = draw_text(f"Timestep: {display_frame} / {max(0, self.makespan - 1)}", y, self.small_font)
        y = draw_text(f"Tasks Finished: {tasks_finished_now}", y, self.small_font)
        y = draw_text(f"Team Size: {self.team_size}", y, self.small_font)
        status_color = (214, 39, 40) if self.paused else (44, 160, 44)
        y = draw_text("Status: Paused" if self.paused else "Status: Playing", y, self.small_font, color=status_color)
        y += 20
        y = draw_text("Task Assignments", y, self.font)
        
        sorted_tasks = sorted(self.tasks, key=lambda t: t[0])
        for task in sorted_tasks:
            task_id = task[0]
            state, event_details = self.get_task_state_at_timestep(task_id, display_frame)

            if state == "assigned" and event_details:
                agent_id, timestep = event_details["agent_id"], event_details["timestep"]
                y = draw_text(f"  Task {task_id}: Agent {agent_id} (at t={timestep})", y, self.small_font)
            elif state == "finished" and event_details:
                agent_id, timestep = event_details["agent_id"], event_details["timestep"]
                y = draw_text(f"  Task {task_id}: Finished by {agent_id} (at t={timestep})", y, self.small_font, color=(100, 100, 100))
            else:
                y = draw_text(f"  Task {task_id}: Unassigned", y, self.small_font)
        
        y += 20
        y = draw_text("Controls", y, self.font)
        y = draw_text("SPACE : Play/Pause", y, self.small_font)
        y = draw_text("→ / ← : Step Frame (paused)", y, self.small_font)
        y = draw_text("ESC     : Quit", y, self.small_font)

    def run(self):
        pygame.event.post(pygame.event.Event(pygame.USEREVENT, {'handler': 'reload_data'}))
        while self.running:
            self._handle_events()
            self._update()
            self._draw()
            self.clock.tick(RENDER_FPS)
        pygame.quit()

if __name__ == '__main__':
    if not os.path.exists(MAP_FILE):
        print(f"--- ERROR: Map file not found at '{MAP_FILE}' ---")
    else:
        visualizer = LiveMAPFVisualizer(map_file=MAP_FILE, json_file=JSON_FILE)
        visualizer.run()