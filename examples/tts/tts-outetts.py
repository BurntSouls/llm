import sys
#import json
#import struct
import requests
import re
import struct
import numpy as np
from concurrent.futures import ThreadPoolExecutor


def fill_hann_window(size, periodic=True):
    if periodic:
        return np.hanning(size + 1)[:-1]
    return np.hanning(size)


def irfft(n_fft, complex_input):
    return np.fft.irfft(complex_input, n=n_fft)


def fold(buffer, n_out, n_win, n_hop, n_pad):
    result = np.zeros(n_out)
    n_frames = len(buffer) // n_win

    for i in range(n_frames):
        start = i * n_hop
        end = start + n_win
        result[start:end] += buffer[i * n_win:(i + 1) * n_win]

    return result[n_pad:-n_pad] if n_pad > 0 else result


def process_frame(args):
    l, n_fft, ST, hann = args
    frame = irfft(n_fft, ST[l])
    frame = frame * hann
    hann2 = hann * hann
    return frame, hann2


def embd_to_audio(embd, n_codes, n_embd, n_thread=4):
    embd = np.asarray(embd, dtype=np.float32).reshape(n_codes, n_embd)

    n_fft = 1280
    n_hop = 320
    n_win = 1280
    n_pad = (n_win - n_hop) // 2
    n_out = (n_codes - 1) * n_hop + n_win

    hann = fill_hann_window(n_fft, True)

    E = np.zeros((n_embd, n_codes), dtype=np.float32)
    for l in range(n_codes):
        for k in range(n_embd):
            E[k, l] = embd[l, k]

    half_embd = n_embd // 2
    S = np.zeros((n_codes, half_embd + 1), dtype=np.complex64)

    for k in range(half_embd):
        for l in range(n_codes):
            mag = E[k, l]
            phi = E[k + half_embd, l]

            mag = np.clip(np.exp(mag), 0, 1e2)
            S[l, k] = mag * np.exp(1j * phi)

    res = np.zeros(n_codes * n_fft)
    hann2_buffer = np.zeros(n_codes * n_fft)

    with ThreadPoolExecutor(max_workers=n_thread) as executor:
        args = [(l, n_fft, S, hann) for l in range(n_codes)]
        results = list(executor.map(process_frame, args))

        for l, (frame, hann2) in enumerate(results):
            res[l*n_fft:(l+1)*n_fft] = frame
            hann2_buffer[l*n_fft:(l+1)*n_fft] = hann2

    audio = fold(res, n_out, n_win, n_hop, n_pad)
    env = fold(hann2_buffer, n_out, n_win, n_hop, n_pad)

    mask = env > 1e-10
    audio[mask] /= env[mask]

    return audio


def save_wav(filename, audio_data, sample_rate):
    num_channels = 1
    bits_per_sample = 16
    bytes_per_sample = bits_per_sample // 8
    data_size = len(audio_data) * bytes_per_sample
    byte_rate = sample_rate * num_channels * bytes_per_sample
    block_align = num_channels * bytes_per_sample
    chunk_size = 36 + data_size  # 36 = size of header minus first 8 bytes

    header = struct.pack(
        '<4sI4s4sIHHIIHH4sI',
        b'RIFF',
        chunk_size,
        b'WAVE',
        b'fmt ',
        16,                # fmt chunk size
        1,                 # audio format (PCM)
        num_channels,
        sample_rate,
        byte_rate,
        block_align,
        bits_per_sample,
        b'data',
        data_size
    )

    audio_data = np.clip(audio_data * 32767, -32768, 32767)
    pcm_data = audio_data.astype(np.int16)

    with open(filename, 'wb') as f:
        f.write(header)
        f.write(pcm_data.tobytes())


def process_text(text: str):
    text = re.sub(r'\d+(\.\d+)?', lambda x: x.group(), text.lower()) # TODO this needs to be fixed
    text = re.sub(r'[-_/,\.\\]', ' ', text)
    text = re.sub(r'[^a-z\s]', '', text)
    text = re.sub(r'\s+', ' ', text).strip()
    return text.split()

# usage:
# python tts-outetts.py http://server-llm:port http://server-dec:port "text"

if len(sys.argv) <= 3:
    print("usage: python tts-outetts.py http://server-llm:port http://server-dec:port \"text\"")
    exit(1)

host_llm = sys.argv[1]
host_dec = sys.argv[2]
text = sys.argv[3]

prefix = """<|im_start|>
<|text_start|>the<|text_sep|>overall<|text_sep|>package<|text_sep|>from<|text_sep|>just<|text_sep|>two<|text_sep|>people<|text_sep|>is<|text_sep|>pretty<|text_sep|>remarkable<|text_sep|>sure<|text_sep|>i<|text_sep|>have<|text_sep|>some<|text_sep|>critiques<|text_sep|>about<|text_sep|>some<|text_sep|>of<|text_sep|>the<|text_sep|>gameplay<|text_sep|>aspects<|text_sep|>but<|text_sep|>its<|text_sep|>still<|text_sep|>really<|text_sep|>enjoyable<|text_sep|>and<|text_sep|>it<|text_sep|>looks<|text_sep|>lovely<|text_sep|>"""

words = process_text(text)
words = "<|text_sep|>".join([i.strip() for i in words])
words += "<|text_end|>\n"

# voice data
# TODO: load from json
#suffix = """<|audio_start|>
#the<|t_0.08|><|code_start|><|257|><|740|><|636|><|913|><|788|><|1703|><|code_end|>
#overall<|t_0.36|><|code_start|><|127|><|201|><|191|><|774|><|700|><|532|><|1056|><|557|><|798|><|298|><|1741|><|747|><|1662|><|1617|><|1702|><|1527|><|368|><|1588|><|1049|><|1008|><|1625|><|747|><|1576|><|728|><|1019|><|1696|><|1765|><|code_end|>
#package<|t_0.56|><|code_start|><|935|><|584|><|1319|><|627|><|1016|><|1491|><|1344|><|1117|><|1526|><|1040|><|239|><|1435|><|951|><|498|><|723|><|1180|><|535|><|789|><|1649|><|1637|><|78|><|465|><|1668|><|901|><|595|><|1675|><|117|><|1009|><|1667|><|320|><|840|><|79|><|507|><|1762|><|1508|><|1228|><|1768|><|802|><|1450|><|1457|><|232|><|639|><|code_end|>
#from<|t_0.19|><|code_start|><|604|><|782|><|1682|><|872|><|1532|><|1600|><|1036|><|1761|><|647|><|1554|><|1371|><|653|><|1595|><|950|><|code_end|>
#just<|t_0.25|><|code_start|><|1782|><|1670|><|317|><|786|><|1748|><|631|><|599|><|1155|><|1364|><|1524|><|36|><|1591|><|889|><|1535|><|541|><|440|><|1532|><|50|><|870|><|code_end|>
#two<|t_0.24|><|code_start|><|1681|><|1510|><|673|><|799|><|805|><|1342|><|330|><|519|><|62|><|640|><|1138|><|565|><|1552|><|1497|><|1552|><|572|><|1715|><|1732|><|code_end|>
#people<|t_0.39|><|code_start|><|593|><|274|><|136|><|740|><|691|><|633|><|1484|><|1061|><|1138|><|1485|><|344|><|428|><|397|><|1562|><|645|><|917|><|1035|><|1449|><|1669|><|487|><|442|><|1484|><|1329|><|1832|><|1704|><|600|><|761|><|653|><|269|><|code_end|>
#is<|t_0.16|><|code_start|><|566|><|583|><|1755|><|646|><|1337|><|709|><|802|><|1008|><|485|><|1583|><|652|><|10|><|code_end|>
#pretty<|t_0.32|><|code_start|><|1818|><|1747|><|692|><|733|><|1010|><|534|><|406|><|1697|><|1053|><|1521|><|1355|><|1274|><|816|><|1398|><|211|><|1218|><|817|><|1472|><|1703|><|686|><|13|><|822|><|445|><|1068|><|code_end|>
#remarkable<|t_0.68|><|code_start|><|230|><|1048|><|1705|><|355|><|706|><|1149|><|1535|><|1787|><|1356|><|1396|><|835|><|1583|><|486|><|1249|><|286|><|937|><|1076|><|1150|><|614|><|42|><|1058|><|705|><|681|><|798|><|934|><|490|><|514|><|1399|><|572|><|1446|><|1703|><|1346|><|1040|><|1426|><|1304|><|664|><|171|><|1530|><|625|><|64|><|1708|><|1830|><|1030|><|443|><|1509|><|1063|><|1605|><|1785|><|721|><|1440|><|923|><|code_end|>
#sure<|t_0.36|><|code_start|><|792|><|1780|><|923|><|1640|><|265|><|261|><|1525|><|567|><|1491|><|1250|><|1730|><|362|><|919|><|1766|><|543|><|1|><|333|><|113|><|970|><|252|><|1606|><|133|><|302|><|1810|><|1046|><|1190|><|1675|><|code_end|>
#i<|t_0.08|><|code_start|><|123|><|439|><|1074|><|705|><|1799|><|637|><|code_end|>
#have<|t_0.16|><|code_start|><|1509|><|599|><|518|><|1170|><|552|><|1029|><|1267|><|864|><|419|><|143|><|1061|><|0|><|code_end|>
#some<|t_0.16|><|code_start|><|619|><|400|><|1270|><|62|><|1370|><|1832|><|917|><|1661|><|167|><|269|><|1366|><|1508|><|code_end|>
#critiques<|t_0.60|><|code_start|><|559|><|584|><|1163|><|1129|><|1313|><|1728|><|721|><|1146|><|1093|><|577|><|928|><|27|><|630|><|1080|><|1346|><|1337|><|320|><|1382|><|1175|><|1682|><|1556|><|990|><|1683|><|860|><|1721|><|110|><|786|><|376|><|1085|><|756|><|1523|><|234|><|1334|><|1506|><|1578|><|659|><|612|><|1108|><|1466|><|1647|><|308|><|1470|><|746|><|556|><|1061|><|code_end|>
#about<|t_0.29|><|code_start|><|26|><|1649|><|545|><|1367|><|1263|><|1728|><|450|><|859|><|1434|><|497|><|1220|><|1285|><|179|><|755|><|1154|><|779|><|179|><|1229|><|1213|><|922|><|1774|><|1408|><|code_end|>
#some<|t_0.23|><|code_start|><|986|><|28|><|1649|><|778|><|858|><|1519|><|1|><|18|><|26|><|1042|><|1174|><|1309|><|1499|><|1712|><|1692|><|1516|><|1574|><|code_end|>
#of<|t_0.07|><|code_start|><|197|><|716|><|1039|><|1662|><|64|><|code_end|>
#the<|t_0.08|><|code_start|><|1811|><|1568|><|569|><|886|><|1025|><|1374|><|code_end|>
#gameplay<|t_0.48|><|code_start|><|1269|><|1092|><|933|><|1362|><|1762|><|1700|><|1675|><|215|><|781|><|1086|><|461|><|838|><|1022|><|759|><|649|><|1416|><|1004|><|551|><|909|><|787|><|343|><|830|><|1391|><|1040|><|1622|><|1779|><|1360|><|1231|><|1187|><|1317|><|76|><|997|><|989|><|978|><|737|><|189|><|code_end|>
#aspects<|t_0.56|><|code_start|><|1423|><|797|><|1316|><|1222|><|147|><|719|><|1347|><|386|><|1390|><|1558|><|154|><|440|><|634|><|592|><|1097|><|1718|><|712|><|763|><|1118|><|1721|><|1311|><|868|><|580|><|362|><|1435|><|868|><|247|><|221|><|886|><|1145|><|1274|><|1284|><|457|><|1043|><|1459|><|1818|><|62|><|599|><|1035|><|62|><|1649|><|778|><|code_end|>
#but<|t_0.20|><|code_start|><|780|><|1825|><|1681|><|1007|><|861|><|710|><|702|><|939|><|1669|><|1491|><|613|><|1739|><|823|><|1469|><|648|><|code_end|>
#its<|t_0.09|><|code_start|><|92|><|688|><|1623|><|962|><|1670|><|527|><|599|><|code_end|>
#still<|t_0.27|><|code_start|><|636|><|10|><|1217|><|344|><|713|><|957|><|823|><|154|><|1649|><|1286|><|508|><|214|><|1760|><|1250|><|456|><|1352|><|1368|><|921|><|615|><|5|><|code_end|>
#really<|t_0.36|><|code_start|><|55|><|420|><|1008|><|1659|><|27|><|644|><|1266|><|617|><|761|><|1712|><|109|><|1465|><|1587|><|503|><|1541|><|619|><|197|><|1019|><|817|><|269|><|377|><|362|><|1381|><|507|><|1488|><|4|><|1695|><|code_end|>
#enjoyable<|t_0.49|><|code_start|><|678|><|501|><|864|><|319|><|288|><|1472|><|1341|><|686|><|562|><|1463|><|619|><|1563|><|471|><|911|><|730|><|1811|><|1006|><|520|><|861|><|1274|><|125|><|1431|><|638|><|621|><|153|><|876|><|1770|><|437|><|987|><|1653|><|1109|><|898|><|1285|><|80|><|593|><|1709|><|843|><|code_end|>
#and<|t_0.15|><|code_start|><|1285|><|987|><|303|><|1037|><|730|><|1164|><|502|><|120|><|1737|><|1655|><|1318|><|code_end|>
#it<|t_0.09|><|code_start|><|848|><|1366|><|395|><|1601|><|1513|><|593|><|1302|><|code_end|>
#looks<|t_0.27|><|code_start|><|1281|><|1266|><|1755|><|572|><|248|><|1751|><|1257|><|695|><|1380|><|457|><|659|><|585|><|1315|><|1105|><|1776|><|736|><|24|><|736|><|654|><|1027|><|code_end|>
#lovely<|t_0.56|><|code_start|><|634|><|596|><|1766|><|1556|><|1306|><|1285|><|1481|><|1721|><|1123|><|438|><|1246|><|1251|><|795|><|659|><|1381|><|1658|><|217|><|1772|><|562|><|952|><|107|><|1129|><|1112|><|467|><|550|><|1079|><|840|><|1615|><|1469|><|1380|><|168|><|917|><|836|><|1827|><|437|><|583|><|67|><|595|><|1087|><|1646|><|1493|><|1677|><|code_end|>"""

# TODO: tokenization is slow for some reason - here is pre-tokenized input
suffix = [ 151667, 198, 1782, 155780, 151669, 151929, 152412, 152308, 152585, 152460, 153375, 151670, 198, 74455,
          155808, 151669, 151799, 151873, 151863, 152446, 152372, 152204, 152728, 152229, 152470, 151970, 153413,
          152419, 153334, 153289, 153374, 153199, 152040, 153260, 152721, 152680, 153297, 152419, 153248, 152400,
          152691, 153368, 153437, 151670, 198, 1722, 155828, 151669, 152607, 152256, 152991, 152299, 152688, 153163,
          153016, 152789, 153198, 152712, 151911, 153107, 152623, 152170, 152395, 152852, 152207, 152461, 153321,
          153309, 151750, 152137, 153340, 152573, 152267, 153347, 151789, 152681, 153339, 151992, 152512, 151751,
          152179, 153434, 153180, 152900, 153440, 152474, 153122, 153129, 151904, 152311, 151670, 198, 1499, 155791,
          151669, 152276, 152454, 153354, 152544, 153204, 153272, 152708, 153433, 152319, 153226, 153043, 152325,
          153267, 152622, 151670, 198, 4250, 155797, 151669, 153454, 153342, 151989, 152458, 153420, 152303, 152271,
          152827, 153036, 153196, 151708, 153263, 152561, 153207, 152213, 152112, 153204, 151722, 152542, 151670, 198,
          19789, 155796, 151669, 153353, 153182, 152345, 152471, 152477, 153014, 152002, 152191, 151734, 152312, 152810,
          152237, 153224, 153169, 153224, 152244, 153387, 153404, 151670, 198, 16069, 155811, 151669, 152265, 151946,
          151808, 152412, 152363, 152305, 153156, 152733, 152810, 153157, 152016, 152100, 152069, 153234, 152317,
          152589, 152707, 153121, 153341, 152159, 152114, 153156, 153001, 153504, 153376, 152272, 152433, 152325,
          151941, 151670, 198, 285, 155788, 151669, 152238, 152255, 153427, 152318, 153009, 152381, 152474, 152680,
          152157, 153255, 152324, 151682, 151670, 198, 32955, 155804, 151669, 153490, 153419, 152364, 152405, 152682,
          152206, 152078, 153369, 152725, 153193, 153027, 152946, 152488, 153070, 151883, 152890, 152489, 153144,
          153375, 152358, 151685, 152494, 152117, 152740, 151670, 198, 37448, 480, 155840, 151669, 151902, 152720,
          153377, 152027, 152378, 152821, 153207, 153459, 153028, 153068, 152507, 153255, 152158, 152921, 151958,
          152609, 152748, 152822, 152286, 151714, 152730, 152377, 152353, 152470, 152606, 152162, 152186, 153071,
          152244, 153118, 153375, 153018, 152712, 153098, 152976, 152336, 151843, 153202, 152297, 151736, 153380,
          153502, 152702, 152115, 153181, 152735, 153277, 153457, 152393, 153112, 152595, 151670, 198, 19098, 155808,
          151669, 152464, 153452, 152595, 153312, 151937, 151933, 153197, 152239, 153163, 152922, 153402, 152034,
          152591, 153438, 152215, 151673, 152005, 151785, 152642, 151924, 153278, 151805, 151974, 153482, 152718,
          152862, 153347, 151670, 198, 72, 155780, 151669, 151795, 152111, 152746, 152377, 153471, 152309, 151670, 198,
          19016, 155788, 151669, 153181, 152271, 152190, 152842, 152224, 152701, 152939, 152536, 152091, 151815, 152733,
          151672, 151670, 198, 14689, 155788, 151669, 152291, 152072, 152942, 151734, 153042, 153504, 152589, 153333,
          151839, 151941, 153038, 153180, 151670, 198, 36996, 8303, 155832, 151669, 152231, 152256, 152835, 152801,
          152985, 153400, 152393, 152818, 152765, 152249, 152600, 151699, 152302, 152752, 153018, 153009, 151992,
          153054, 152847, 153354, 153228, 152662, 153355, 152532, 153393, 151782, 152458, 152048, 152757, 152428,
          153195, 151906, 153006, 153178, 153250, 152331, 152284, 152780, 153138, 153319, 151980, 153142, 152418,
          152228, 152733, 151670, 198, 9096, 155801, 151669, 151698, 153321, 152217, 153039, 152935, 153400, 152122,
          152531, 153106, 152169, 152892, 152957, 151851, 152427, 152826, 152451, 151851, 152901, 152885, 152594,
          153446, 153080, 151670, 198, 14689, 155795, 151669, 152658, 151700, 153321, 152450, 152530, 153191, 151673,
          151690, 151698, 152714, 152846, 152981, 153171, 153384, 153364, 153188, 153246, 151670, 198, 1055, 155779,
          151669, 151869, 152388, 152711, 153334, 151736, 151670, 198, 1782, 155780, 151669, 153483, 153240, 152241,
          152558, 152697, 153046, 151670, 198, 5804, 1363, 155820, 151669, 152941, 152764, 152605, 153034, 153434,
          153372, 153347, 151887, 152453, 152758, 152133, 152510, 152694, 152431, 152321, 153088, 152676, 152223,
          152581, 152459, 152015, 152502, 153063, 152712, 153294, 153451, 153032, 152903, 152859, 152989, 151748,
          152669, 152661, 152650, 152409, 151861, 151670, 198, 300, 7973, 155828, 151669, 153095, 152469, 152988,
          152894, 151819, 152391, 153019, 152058, 153062, 153230, 151826, 152112, 152306, 152264, 152769, 153390,
          152384, 152435, 152790, 153393, 152983, 152540, 152252, 152034, 153107, 152540, 151919, 151893, 152558,
          152817, 152946, 152956, 152129, 152715, 153131, 153490, 151734, 152271, 152707, 151734, 153321, 152450,
          151670, 198, 8088, 155792, 151669, 152452, 153497, 153353, 152679, 152533, 152382, 152374, 152611, 153341,
          153163, 152285, 153411, 152495, 153141, 152320, 151670, 198, 1199, 155781, 151669, 151764, 152360, 153295,
          152634, 153342, 152199, 152271, 151670, 198, 43366, 155799, 151669, 152308, 151682, 152889, 152016, 152385,
          152629, 152495, 151826, 153321, 152958, 152180, 151886, 153432, 152922, 152128, 153024, 153040, 152593,
          152287, 151677, 151670, 198, 53660, 155808, 151669, 151727, 152092, 152680, 153331, 151699, 152316, 152938,
          152289, 152433, 153384, 151781, 153137, 153259, 152175, 153213, 152291, 151869, 152691, 152489, 151941,
          152049, 152034, 153053, 152179, 153160, 151676, 153367, 151670, 198, 268, 4123, 480, 155821, 151669, 152350,
          152173, 152536, 151991, 151960, 153144, 153013, 152358, 152234, 153135, 152291, 153235, 152143, 152583,
          152402, 153483, 152678, 152192, 152533, 152946, 151797, 153103, 152310, 152293, 151825, 152548, 153442,
          152109, 152659, 153325, 152781, 152570, 152957, 151752, 152265, 153381, 152515, 151670, 198, 437, 155787,
          151669, 152957, 152659, 151975, 152709, 152402, 152836, 152174, 151792, 153409, 153327, 152990, 151670, 198,
          275, 155781, 151669, 152520, 153038, 152067, 153273, 153185, 152265, 152974, 151670, 198, 94273, 155799,
          151669, 152953, 152938, 153427, 152244, 151920, 153423, 152929, 152367, 153052, 152129, 152331, 152257,
          152987, 152777, 153448, 152408, 151696, 152408, 152326, 152699, 151670, 198, 385, 16239, 155828, 151669,
          152306, 152268, 153438, 153228, 152978, 152957, 153153, 153393, 152795, 152110, 152918, 152923, 152467,
          152331, 153053, 153330, 151889, 153444, 152234, 152624, 151779, 152801, 152784, 152139, 152222, 152751,
          152512, 153287, 153141, 153052, 151840, 152589, 152508, 153499, 152109, 152255, 151739, 152267, 152759,
          153318, 153165, 153349, 151670, ]

response = requests.post(
    host_llm + "/completion",
    json={
        "prompt": [prefix + words, *suffix],
        "n_predict": 1024,
        "cache_prompt": True,
        "return_tokens": True,
        "samplers": ["top_k"],
        "top_k": 16,
        "seed": 1003,
    }
)

response_json = response.json()

#print(json.dumps(response_json, indent=4))
#print(json.dumps(response_json["prompt"], indent=4).replace("\\n", "\n"))
#print(json.dumps(response_json["timings"], indent=4))
#print(json.dumps(response_json["tokens"], indent=4))

codes = response_json["tokens"]

codes = [t - 151672 for t in codes if t >= 151672 and t <= 155772]

response = requests.post(
    host_dec + "/embeddings",
    json={
        "input": [*codes],
    }
)

response_json = response.json()

#print(json.dumps(response_json, indent=4))

# spectrogram
embd = response_json[0]["embedding"]

n_codes = len(embd)
n_embd = len(embd[0])

print('spectrogram generated: n_codes: %d, n_embd: %d' % (n_codes, n_embd))

# post-process the spectrogram to convert to audio
print('converting to audio ...')
audio = embd_to_audio(embd, n_codes, n_embd)
print('audio generated: %d samples' % len(audio))

filename = "output.wav"
sample_rate = 24000 # sampling rate

# zero out first 0.25 seconds
audio[:24000 // 4] = 0.0

save_wav(filename, audio, sample_rate)
print('audio written to file "%s"' % filename)
