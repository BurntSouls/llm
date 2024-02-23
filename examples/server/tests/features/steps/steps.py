import asyncio
import json
import os
import re
import socket
import subprocess
import time
from contextlib import closing
from re import RegexFlag

import aiohttp
import openai
import requests
from behave import step
from behave.api.async_step import async_run_until_complete


@step(u"a server listening on {server_fqdn}:{server_port}")
def step_server_config(context, server_fqdn, server_port):
    context.server_fqdn = server_fqdn
    context.server_port = int(server_port)

    context.base_url = f'http://{context.server_fqdn}:{context.server_port}'

    context.model_alias = None
    context.n_ctx = None
    context.n_predict = None
    context.n_server_predict = None
    context.n_slots = None
    context.server_api_key = None
    context.server_continuous_batching = False
    context.server_embeddings = False
    context.server_seed = None
    context.user_api_key = None

    context.completions = []
    context.concurrent_completion_tasks = []
    context.prompts = []


@step(u'a model file {model_file}')
def step_model_file(context, model_file):
    context.model_file = model_file


@step(u'a model alias {model_alias}')
def step_model_alias(context, model_alias):
    context.model_alias = model_alias


@step(u'{seed} as server seed')
def step_seed(context, seed):
    context.server_seed = int(seed)


@step(u'{n_ctx} KV cache size')
def step_n_ctx(context, n_ctx):
    context.n_ctx = int(n_ctx)


@step(u'{n_slots} slots')
def step_n_slots(context, n_slots):
    context.n_slots = int(n_slots)


@step(u'{n_predict} server max tokens to predict')
def step_server_n_predict(context, n_predict):
    context.n_server_predict = int(n_predict)


@step(u'continuous batching')
def step_server_continuous_batching(context):
    context.server_continuous_batching = True

@step(u'embeddings extraction')
def step_server_embeddings(context):
    context.server_embeddings = True


@step(u"the server is starting")
def step_start_server(context):
    start_server_background(context)
    attempts = 0
    while True:
        with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as sock:
            result = sock.connect_ex((context.server_fqdn, context.server_port))
            if result == 0:
                print("server started!")
                return
            attempts += 1
            if attempts > 20:
                assert False, "server not started"
            print("waiting for server to start...")
            time.sleep(0.1)


@step(u"the server is {expecting_status}")
@async_run_until_complete
async def step_wait_for_the_server_to_be_started(context, expecting_status):
    match expecting_status:
        case 'healthy':
            await wait_for_health_status(context, context.base_url, 200, 'ok')

        case 'ready' | 'idle':
            await wait_for_health_status(context, context.base_url, 200, 'ok',
                                         params={'fail_on_no_slot': 0, 'include_slots': 0},
                                         slots_idle=context.n_slots,
                                         slots_processing=0,
                                         expected_slots=[{'id': slot_id, 'state': 0}
                                                         for slot_id in range(context.n_slots)])
        case 'busy':
            await wait_for_health_status(context, context.base_url, 503,
                                         'no slot available',
                                         params={'fail_on_no_slot': 0, 'include_slots': 0},
                                         slots_idle=0,
                                         slots_processing=context.n_slots,
                                         expected_slots=[{'id': slot_id, 'state': 1}
                                                         for slot_id in range(context.n_slots)])
        case _:
            assert False, "unknown status"


@step(u'all slots are {expected_slot_status_string}')
@async_run_until_complete
async def step_all_slots_status(context, expected_slot_status_string):
    match expected_slot_status_string:
        case 'idle':
            expected_slot_status = 0
        case 'busy':
            expected_slot_status = 1
        case _:
            assert False, "unknown status"

    expected_slots = [{'id': slot_id, 'state': expected_slot_status}
                      for slot_id in range(context.n_slots)]
    await request_slots_status(context, expected_slots)


@step(u'a completion request with {api_error} api error')
@async_run_until_complete
async def step_request_completion(context, api_error):
    expect_api_error = api_error == 'raised'
    completion = await request_completion(context.prompts.pop(),
                                          context.base_url,
                                          n_predict=context.n_predict,
                                          server_seed=context.server_seed,
                                          expect_api_error=expect_api_error,
                                          user_api_key=context.user_api_key)
    context.completions.append(completion)
    print(f"Completion response: {completion}")
    if expect_api_error:
        assert completion == 401, f"completion must be an 401 status code: {completion}"


@step(u'{predicted_n} tokens are predicted matching {re_content}')
def step_n_tokens_predicted_with_content(context, predicted_n, re_content):
    assert_n_tokens_predicted(context.completions.pop(), int(predicted_n), re_content)


@step(u'{predicted_n} tokens are predicted')
def step_n_tokens_predicted(context, predicted_n):
    assert_n_tokens_predicted(context.completions.pop(), int(predicted_n))


@step(u'a user prompt {user_prompt}')
def step_user_prompt(context, user_prompt):
    context.prompts.append(user_prompt)


@step(u'a system prompt {system_prompt}')
def step_system_prompt(context, system_prompt):
    context.system_prompt = system_prompt


@step(u'a model {model}')
def step_model(context, model):
    context.model = model


@step(u'{max_tokens} max tokens to predict')
def step_max_tokens(context, max_tokens):
    context.n_predict = int(max_tokens)


@step(u'streaming is {enable_streaming}')
def step_streaming(context, enable_streaming):
    context.enable_streaming = enable_streaming == 'enabled'


@step(u'a user api key {user_api_key}')
def step_user_api_key(context, user_api_key):
    context.user_api_key = user_api_key


@step(u'a user api key ')
def step_no_user_api_key(context):
    context.user_api_key = None


@step(u'no user api key')
def step_no_user_api_key(context):
    context.user_api_key = None


@step(u'a server api key {server_api_key}')
def step_server_api_key(context, server_api_key):
    context.server_api_key = server_api_key


@step(u'an OAI compatible chat completions request with {api_error} api error')
@async_run_until_complete
async def step_oai_chat_completions(context, api_error):
    print(f"Submitting OAI compatible completions request...")
    expect_api_error = api_error == 'raised'
    completion = await oai_chat_completions(context.prompts.pop(),
                                            context.system_prompt,
                                            context.base_url,
                                            False,
                                            model=context.model if hasattr(context, 'model') else None,

                                            n_predict=context.n_predict
                                            if hasattr(context, 'n_predict') else None,

                                            enable_streaming=context.enable_streaming
                                            if hasattr(context, 'enable_streaming') else None,

                                            server_seed=context.server_seed
                                            if hasattr(context, 'server_seed') else None,

                                            user_api_key=context.user_api_key
                                            if hasattr(context, 'user_api_key') else None,

                                            expect_api_error=expect_api_error)
    context.completions.append(completion)
    print(f"Completion response: {completion}")
    if expect_api_error:
        assert completion == 401, f"completion must be an 401 status code: {completion}"

    print(f"Completion response: {completion}")


@step(u'a prompt')
def step_a_prompt(context):
    context.prompts.append(context.text)


@step(u'a prompt {prompt}')
def step_a_prompt_prompt(context, prompt):
    context.prompts.append(prompt)


@step(u'concurrent completion requests')
@async_run_until_complete()
async def step_concurrent_completion_requests(context):
    await concurrent_completion_requests(context,
                                         request_completion,
                                         # prompt is inserted automatically
                                         context.base_url,
                                         n_predict=context.n_predict if hasattr(context, 'n_predict') else None,
                                         server_seed=context.server_seed if hasattr(context, 'server_seed') else None,
                                         user_api_key=context.user_api_key if hasattr(context,
                                                                                      'user_api_key') else None)


@step(u'concurrent OAI completions requests')
@async_run_until_complete
async def step_oai_chat_completions(context):
    await concurrent_completion_requests(context, oai_chat_completions,
                                         # user_prompt is inserted automatically
                                         context.system_prompt,
                                         context.base_url,
                                         True,  # async_client
                                         model=context.model
                                         if hasattr(context, 'model') else None,
                                         n_predict=context.n_predict
                                         if hasattr(context, 'n_predict') else None,
                                         enable_streaming=context.enable_streaming
                                         if hasattr(context, 'enable_streaming') else None,
                                         server_seed=context.server_seed
                                         if hasattr(context, 'server_seed') else None,
                                         user_api_key=context.user_api_key
                                         if hasattr(context, 'user_api_key') else None)


@async_run_until_complete
@step(u'all prompts are predicted')
async def step_impl(context):
    await all_prompts_are_predicted(context)


@step(u'all prompts are predicted with {n_predict} tokens')
@async_run_until_complete
async def step_all_prompts_are_predicted(context, n_predict):
    expected_predicted_n = int(n_predict)
    await all_prompts_are_predicted(context, expected_predicted_n)


async def all_prompts_are_predicted(context, expected_predicted_n=None):
    n_completions = await gather_concurrent_completions_tasks(context)
    assert n_completions > 0
    for i in range(n_completions):
        assert_n_tokens_predicted(context.completions.pop(), expected_predicted_n=expected_predicted_n)


@step(u'embeddings are computed for')
def step_compute_embedding(context):
    response = requests.post(f'{context.base_url}/embedding', json={
        "content": context.text,
    })
    assert response.status_code == 200
    context.embeddings = response.json()['embedding']


@step(u'embeddings are generated')
def step_compute_embeddings(context):
    assert len(context.embeddings) > 0
    embeddings_computed = False
    for emb in context.embeddings:
        if emb != 0:
            embeddings_computed = True
    assert embeddings_computed, f"Embeddings: {context.embeddings}"


@step(u'an OAI compatible embeddings computation request for')
def step_oai_compute_embedding(context):
    openai.api_key = 'nope'  # openai client always expects an api_keu
    if context.user_api_key is not None:
        openai.api_key = context.user_api_key
    openai.api_base = f'{context.base_url}/v1'
    embeddings = openai.Embedding.create(
        model=context.model,
        input=context.text,
    )
    context.embeddings = embeddings


@step(u'tokenizing')
def step_tokenize(context):
    context.tokenized_text = context.text
    response = requests.post(f'{context.base_url}/tokenize', json={
        "content": context.tokenized_text,
    })
    assert response.status_code == 200
    context.tokens = response.json()['tokens']


@step(u'tokens can be detokenize')
def step_detokenize(context):
    assert len(context.tokens) > 0
    response = requests.post(f'{context.base_url}/detokenize', json={
        "tokens": context.tokens,
    })
    assert response.status_code == 200
    # SPM tokenizer adds a whitespace prefix: https://github.com/google/sentencepiece/issues/15
    assert context.tokenized_text == response.json()['content'].strip()


@step(u'an OPTIONS request is sent from {origin}')
def step_options_request(context, origin):
    options_response = requests.options(f'{context.base_url}/v1/chat/completions',
                                        headers={"Origin": origin})
    assert options_response.status_code == 200
    context.options_response = options_response


@step(u'CORS header {cors_header} is set to {cors_header_value}')
def step_check_options_header_value(context, cors_header, cors_header_value):
    assert context.options_response.headers[cors_header] == cors_header_value


async def concurrent_completion_requests(context, f_completion, *args, **kwargs):
    n_prompts = len(context.prompts)
    print(f"starting {n_prompts} concurrent completion requests...")
    assert n_prompts > 0
    for prompt_no in range(n_prompts):
        shifted_args = [context.prompts.pop(), *args]
        context.concurrent_completion_tasks.append(asyncio.create_task(f_completion(*shifted_args, **kwargs)))
    await asyncio.sleep(0.1)


async def request_completion(prompt,
                             base_url,
                             n_predict=None,
                             server_seed=None,
                             expect_api_error=None,
                             user_api_key=None):
    print(f"Sending completion request: {prompt}")
    origin = "my.super.domain"
    headers = {
        'Origin': origin
    }
    if user_api_key is not None:
        print(f"Set user_api_key: {user_api_key}")
        headers['Authorization'] = f'Bearer {user_api_key}'

    async with aiohttp.ClientSession() as session:
        async with session.post(f'{base_url}/completion',
                                json={
                                    "prompt": prompt,
                                    "n_predict": int(n_predict) if n_predict is not None else -1,
                                    "seed": server_seed if server_seed is not None else 42
                                },
                                headers=headers) as response:
            if expect_api_error is None or not expect_api_error:
                assert response.status == 200
                assert response.headers['Access-Control-Allow-Origin'] == origin
                return await response.json()
            else:
                return response.status


async def oai_chat_completions(user_prompt,
                               system_prompt,
                               base_url,
                               async_client,
                               model=None,
                               n_predict=None,
                               enable_streaming=None,
                               server_seed=None,
                               user_api_key=None,
                               expect_api_error=None):
    print(f"Sending OAI Chat completions request: {user_prompt}")
    # openai client always expects an api key
    user_api_key = user_api_key if user_api_key is not None else 'nope'
    seed = server_seed if server_seed is not None else 42
    enable_streaming = enable_streaming if enable_streaming is not None else False
    payload = {
        "messages": [
            {
                "role": "system",
                "content": system_prompt,
            },
            {
                "role": "user",
                "content": user_prompt,
            }
        ],
        "model": model,
        "max_tokens": n_predict,
        "stream": enable_streaming,
        "seed": seed
    }
    completion_response = {
        'content': '',
        'timings': {
            'predicted_n': 0
        }
    }
    if async_client:
        origin = 'llama.cpp'
        headers = {'Authorization': f'Bearer {user_api_key}', 'Origin': origin}
        async with aiohttp.ClientSession() as session:
            async with session.post(f'{base_url}/v1/chat/completions',
                                    json=payload,
                                    headers=headers) as response:
                if enable_streaming:
                    # FIXME: does not work; the server is generating only one token
                    print("DEBUG payload", payload)
                    assert response.status == 200
                    assert response.headers['Access-Control-Allow-Origin'] == origin
                    assert response.headers['Content-Type'] == "text/event-stream"

                    async for line_in_bytes in response.content:
                        line = line_in_bytes.decode('utf8')
                        event_data = line.split(': ', 1)
                        assert event_data[0] == 'data', f'{event_data}'
                        chunk_raw = event_data[1]

                        chunk = json.loads(chunk_raw)
                        assert len(chunk['choices']) == 1
                        delta = chunk['choices'][0]['delta']
                        if 'content' in delta:
                            completion_response['content'] += delta['content']
                            completion_response['timings']['predicted_n'] += 1
                        print(f"DEBUG completion_response: {completion_response}")
                else:
                    if expect_api_error is None or not expect_api_error:
                        assert response.status == 200
                        assert response.headers['Access-Control-Allow-Origin'] == origin
                        assert response.headers['Content-Type'] == "application/json; charset=utf-8"
                        chat_completion_raw = await response.json()
                        completion_response = {
                            'content': chat_completion_raw['choices'][0]['message'],
                            'timings': {
                                'predicted_n': chat_completion_raw['usage']['completion_tokens']
                            }
                        }
                    else:
                        return response.status
    else:
        try:
            openai.api_key = user_api_key
            openai.api_base = f'{base_url}/v1/chat'
            chat_completion = openai.Completion.create(
                messages=payload['messages'],
                model=model,
                max_tokens=n_predict,
                stream=enable_streaming,
                seed=seed
            )
        except openai.error.APIError as e:
            if expect_api_error is not None and expect_api_error:
                return 401
            else:
                assert False, f'error raised: {e}'

        if enable_streaming:
            for chunk in chat_completion:
                assert len(chunk.choices) == 1
                delta = chunk.choices[0].delta
                if 'content' in delta:
                    completion_response['content'] += delta['content']
                    completion_response['timings']['predicted_n'] += 1
        else:
            assert len(chat_completion.choices) == 1
            completion_response = {
                'content': chat_completion.choices[0].message.content,
                'timings': {
                    'predicted_n': chat_completion.usage.completion_tokens
                }
            }
    print("OAI response formatted to llama.cpp:", completion_response)
    return completion_response


def assert_n_tokens_predicted(completion_response, expected_predicted_n=None, re_content=None):
    content = completion_response['content']
    n_predicted = completion_response['timings']['predicted_n']
    assert len(content) > 0, "no token predicted"
    if expected_predicted_n is not None:
        assert n_predicted == expected_predicted_n, (f'invalid number of tokens predicted:'
                                                     f' {n_predicted} <> {expected_predicted_n}')
    if re_content is not None:
        re_content = '^.*' + re_content.replace('<or>', '|') + '.*$'
        assert re.match(re_content, content, flags=RegexFlag.IGNORECASE | RegexFlag.MULTILINE | RegexFlag.DOTALL), (
            f'invalid tokens predicted:'
            f' ```\n{content}\n``` do not match /{re_content}/')


async def gather_concurrent_completions_tasks(context):
    n_completion_tasks = len(context.concurrent_completion_tasks)
    print(f"Waiting for all {n_completion_tasks} completion responses...")
    for task_no in range(n_completion_tasks):
        context.completions.append(await context.concurrent_completion_tasks.pop())
    n_completions = len(context.completions)
    return n_completions


async def wait_for_health_status(context,
                                 base_url,
                                 expected_http_status_code,
                                 expected_health_status,
                                 params=None,
                                 slots_idle=None,
                                 slots_processing=None,
                                 expected_slots=None):
    print(f"Starting checking for health for expected_health_status={expected_health_status}")
    timeout = 3  # seconds
    interval = 0.5
    counter = 0
    async with aiohttp.ClientSession() as session:
        while True:
            async with await session.get(f'{base_url}/health', params=params) as health_response:
                status_code = health_response.status
                health = await health_response.json()
                print(f"HEALTH - response for expected health status='{expected_health_status}' on "
                      f"'{base_url}/health'?{params} is {health}")
                if (status_code == expected_http_status_code
                        and health['status'] == expected_health_status
                        and (slots_idle is None or health['slots_idle'] == slots_idle)
                        and (slots_processing is None or health['slots_processing'] == slots_processing)):
                    if expected_slots is not None:
                        assert_slots_status(health['slots'], expected_slots)
                    return
                if (status_code == expected_http_status_code
                        and health['status'] == expected_health_status
                        and (slots_idle is None or health['slots_idle'] == slots_idle)
                        and (slots_processing is None or health['slots_processing'] == slots_processing)):
                    if expected_slots is not None:
                        assert_slots_status(health['slots'], expected_slots)
                    return
            await asyncio.sleep(interval)

            counter += interval
            if counter >= timeout:
                # Sometimes health requests are triggered after completions are predicted
                if expected_http_status_code == 503:
                    if len(context.completions) == 0:
                        print("\x1b[33;42mWARNING: forcing concurrents completions tasks,"
                              " busy health check missed\x1b[0m")
                        n_completions = await gather_concurrent_completions_tasks(context)
                        if n_completions > 0:
                            return

                assert False, 'timeout exceeded'


async def request_slots_status(context, expected_slots):
    async with aiohttp.ClientSession() as session:
        async with await session.get(f'{context.base_url}/slots') as slots_response:
            assert slots_response.status == 200
            slots = await slots_response.json()
            assert_slots_status(slots, expected_slots)


def assert_slots_status(slots, expected_slots):
    assert len(slots) == len(expected_slots)
    for slot_id, (expected, slot) in enumerate(zip(expected_slots, slots)):
        for key in expected:
            assert expected[key] == slot[key], (f"invalid slot {slot_id}"
                                                f" expected[{key}] != slot[{key}]"
                                                f" = {expected[key]} != {slot[key]}")


def start_server_background(context):
    context.server_path = '../../../build/bin/server'
    if 'LLAMA_SERVER_BIN_PATH' in os.environ:
        context.server_path = os.environ['LLAMA_SERVER_BIN_PATH']
    server_args = [
        '--host', context.server_fqdn,
        '--port', context.server_port,
        '--model', context.model_file
    ]
    if context.server_continuous_batching:
        server_args.append('--cont-batching')
    if context.server_embeddings:
        server_args.append('--embedding')
    if context.model_alias is not None:
        server_args.extend(['--alias', context.model_alias])
    if context.server_seed is not None:
        server_args.extend(['--alias', context.model_alias])
    if context.n_ctx is not None:
        server_args.extend(['--ctx-size', context.n_ctx])
    if context.n_slots is not None:
        server_args.extend(['--parallel', context.n_slots])
    if context.n_server_predict is not None:
        server_args.extend(['--n-predict', context.n_server_predict])
    if context.server_api_key is not None:
        server_args.extend(['--api-key', context.server_api_key])
    print(f"starting server with: {context.server_path}", *server_args)
    context.server_process = subprocess.Popen(
        [str(arg) for arg in [context.server_path, *server_args]],
        close_fds=True)
    print(f"server pid={context.server_process.pid}")
