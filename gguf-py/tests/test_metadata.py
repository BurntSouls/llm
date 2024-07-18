#!/usr/bin/env python3

import unittest
from pathlib import Path
import os
import sys

# Necessary to load the local gguf package
if "NO_LOCAL_GGUF" not in os.environ and (Path(__file__).parent.parent.parent / 'gguf-py').exists():
    sys.path.insert(0, str(Path(__file__).parent.parent))

import gguf


class TestMetadataMethod(unittest.TestCase):

    def test_id_to_title(self):
        self.assertEqual(gguf.Metadata.id_to_title("Mixtral-8x7B-Instruct-v0.1"), "Mixtral 8x7B Instruct v0.1")
        self.assertEqual(gguf.Metadata.id_to_title("Meta-Llama-3-8B"), "Meta Llama 3 8B")
        self.assertEqual(gguf.Metadata.id_to_title("hermes-2-pro-llama-3-8b-DPO"), "Hermes 2 Pro Llama 3 8b DPO")

    def test_get_model_id_components(self):
        # This is the basic standard form with organization marker
        self.assertEqual(gguf.Metadata.get_model_id_components("Mistral/Mixtral-8x7B-Instruct-v0.1"),
                         ('Mixtral-8x7B-Instruct-v0.1', "Mistral", 'Mixtral', 'Instruct', 'v0.1', '8x7B'))

        # Similar to basic standard form but without organization marker
        self.assertEqual(gguf.Metadata.get_model_id_components("Mixtral-8x7B-Instruct-v0.1"),
                         ('Mixtral-8x7B-Instruct-v0.1', None, 'Mixtral', 'Instruct', 'v0.1', '8x7B'))

        # Missing version
        self.assertEqual(gguf.Metadata.get_model_id_components("Mixtral-8x7B-Instruct"),
                         ('Mixtral-8x7B-Instruct', None, 'Mixtral', 'Instruct', None, '8x7B'))

        # Missing finetune
        self.assertEqual(gguf.Metadata.get_model_id_components("Mixtral-8x7B-v0.1"),
                         ('Mixtral-8x7B-v0.1', None, 'Mixtral', None, 'v0.1', '8x7B'))

        # Base name and size label only
        self.assertEqual(gguf.Metadata.get_model_id_components("Mixtral-8x7B"),
                         ('Mixtral-8x7B', None, 'Mixtral', None, None, '8x7B'))

        # Base name and version only
        self.assertEqual(gguf.Metadata.get_model_id_components("Mixtral-v0.1"),
                         ('Mixtral-v0.1', None, 'Mixtral', None, 'v0.1', None))

        ## Edge Cases ##

        # This is too ambiguous... best to err on caution and output nothing
        self.assertEqual(gguf.Metadata.get_model_id_components("Mixtral"),
                         ('Mixtral', None, None, None, None, None))

        # Basename has numbers mixed in and also size label provided. Must avoid capturing number in basename
        self.assertEqual(gguf.Metadata.get_model_id_components("NousResearch/Meta-Llama-3-8B"),
                         ('Meta-Llama-3-8B', "NousResearch", 'Meta-Llama-3', None, None, '8B'))

        # Can't detect all non standard form in a heuristically safe way... best to err in caution and output nothing...
        self.assertEqual(gguf.Metadata.get_model_id_components("Qwen1.5-MoE-A2.7B-Chat"),
                         ('Qwen1.5-MoE-A2.7B-Chat', None, 'Qwen1.5-MoE', 'Chat', None, 'A2.7B'))

        # Capture 'sub size labels' e.g. A14B in '57B-A14B' usually refers to activated params/weight count
        self.assertEqual(gguf.Metadata.get_model_id_components("Qwen2-57B-A14B-Instruct"),
                         ('Qwen2-57B-A14B-Instruct', None, 'Qwen2', 'Instruct', None, '57B-A14B'))

        # Check that it can handle a real model id with no version code
        # Note that 4k in this string is non standard and microsoft were referring to context length rather than weight count
        self.assertEqual(gguf.Metadata.get_model_id_components("microsoft/Phi-3-mini-4k-instruct", 4 * 10**9),
                         ('Phi-3-mini-4k-instruct', 'microsoft', 'Phi-3', '4k-instruct', None, 'mini'))

        # There is some legitimate models with only thousands of parameters
        self.assertEqual(gguf.Metadata.get_model_id_components("delphi-suite/stories-llama2-50k", 50 * 10**3),
                         ('stories-llama2-50k', 'delphi-suite', 'stories-llama2', None, None, '50k'))

        # None standard and not easy to disambiguate
        self.assertEqual(gguf.Metadata.get_model_id_components("DeepSeek-Coder-V2-Lite-Instruct"),
                         ('DeepSeek-Coder-V2-Lite-Instruct', None, 'DeepSeek-Coder-V2-Lite', 'Instruct', None, None))

        # This is a real model_id where they append 2DPO to refer to Direct Preference Optimization
        self.assertEqual(gguf.Metadata.get_model_id_components("crestf411/daybreak-kunoichi-2dpo-7b"),
                         ('daybreak-kunoichi-2dpo-7b', 'crestf411', 'daybreak-kunoichi', '2dpo', None, '7B'))

        # This is a real model id where the weight size has a decimal point
        self.assertEqual(gguf.Metadata.get_model_id_components("Qwen2-0.5B-Instruct"),
                         ('Qwen2-0.5B-Instruct', None, 'Qwen2', 'Instruct', None, '0.5B'))

        # Uses an underscore in the size label
        self.assertEqual(gguf.Metadata.get_model_id_components("smallcloudai/Refact-1_6B-fim"),
                         ('Refact-1_6B-fim', 'smallcloudai', 'Refact', 'fim', None, '1.6B'))

    def test_apply_metadata_heuristic_from_model_card(self):
        model_card = {
            'tags': ['Llama-3', 'instruct', 'finetune', 'chatml', 'DPO', 'RLHF', 'gpt4', 'synthetic data', 'distillation', 'function calling', 'json mode', 'axolotl'],
            'model-index': [{'name': 'Mixtral-8x7B-Instruct-v0.1', 'results': []}],
            'language': ['en'],
            'datasets': ['teknium/OpenHermes-2.5'],
            'widget': [{'example_title': 'Hermes 2 Pro', 'messages': [{'role': 'system', 'content': 'You are a sentient, superintelligent artificial general intelligence, here to teach and assist me.'}, {'role': 'user', 'content': 'Write a short story about Goku discovering kirby has teamed up with Majin Buu to destroy the world.'}]}],
            'base_model': ["EmbeddedLLM/Mistral-7B-Merge-14-v0", "janai-hq/trinity-v1"]
        }
        got = gguf.Metadata.apply_metadata_heuristic(gguf.Metadata(), model_card, None, None)
        expect = gguf.Metadata()
        expect.base_models=[{'name': 'Mistral 7B Merge 14 v0', 'organization': 'EmbeddedLLM', 'version': 'v0', 'repo_url': 'https://huggingface.co/EmbeddedLLM/Mistral-7B-Merge-14-v0'}, {'name': 'Trinity v1', 'organization': 'Janai Hq', 'version': 'v1', 'repo_url': 'https://huggingface.co/janai-hq/trinity-v1'}]
        expect.tags=['Llama-3', 'instruct', 'finetune', 'chatml', 'DPO', 'RLHF', 'gpt4', 'synthetic data', 'distillation', 'function calling', 'json mode', 'axolotl']
        expect.languages=['en']
        expect.datasets=['teknium/OpenHermes-2.5']

        self.assertEqual(got, expect)

    def test_apply_metadata_heuristic_from_hf_parameters(self):
        hf_params = {"_name_or_path": "./hermes-2-pro-llama-3-8b-DPO"}
        got = gguf.Metadata.apply_metadata_heuristic(gguf.Metadata(), model_card=None, hf_params=hf_params, model_path=None)
        expect = gguf.Metadata(name='Hermes 2 Pro Llama 3 8b DPO', finetune='DPO', basename='hermes-2-pro-llama-3', size_label='8B')
        self.assertEqual(got, expect)

    def test_apply_metadata_heuristic_from_model_dir(self):
        model_dir_path = Path("./hermes-2-pro-llama-3-8b-DPO")
        got = gguf.Metadata.apply_metadata_heuristic(gguf.Metadata(), model_card=None, hf_params=None, model_path=model_dir_path)
        expect = gguf.Metadata(name='Hermes 2 Pro Llama 3 8b DPO', finetune='DPO', basename='hermes-2-pro-llama-3', size_label='8B')
        self.assertEqual(got, expect)


if __name__ == "__main__":
    unittest.main()
